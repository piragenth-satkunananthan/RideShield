package com.rideshield.phone.network

import android.os.SystemClock
import android.util.Log
import com.rideshield.phone.protocol.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import okhttp3.*
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * Native Android OkHttp WebSocket Client implementing the exact RSW1 protocol.
 * Conforms to:
 *  - dev_version/rideshield-core/lib/RideShieldCore/src/phone.*
 *  - dev_version/rideshield-core/src/main.cpp
 *  - dev_version/rideshield-core/docs/PHONE_PROTOCOL.md
 */
class RswWebSocketClient(
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() },
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
) {
    private val tag = "RswWebSocketClient"

    private val okHttpClient: OkHttpClient = OkHttpClient.Builder()
        .readTimeout(10, TimeUnit.SECONDS)
        .writeTimeout(5, TimeUnit.SECONDS)
        .connectTimeout(5, TimeUnit.SECONDS)
        .pingInterval(0, TimeUnit.SECONDS) // Controller ignores WebSocket ping/pong
        .retryOnConnectionFailure(true)
        .build()

    val clockSynchronizer = RswClockSynchronizer(clockProvider)

    private val _telemetry = MutableStateFlow(RswClientTelemetry())
    val telemetry: StateFlow<RswClientTelemetry> = _telemetry.asStateFlow()

    private var webSocket: WebSocket? = null
    private val isRunning = AtomicBoolean(false)

    // Connection parameters
    private var serverUrl: String = RswConstants.DEFAULT_CONTROLLER_URL
    private var authToken: String = RswConstants.DEFAULT_TOKEN

    // Protocol state
    @Volatile private var sessionId: Long = 0L
    private val sequenceCounter = AtomicLong(0L)
    @Volatile private var authenticated: Boolean = false
    @Volatile private var lastFrontCaptureMs: Long = 0L
    @Volatile private var lastRearCaptureMs: Long = 0L

    // Rate limiting: max 40 packets/sec (controller hard drop limit is 50 pkts/s)
    private val rateWindowStartMs = AtomicLong(0L)
    private val packetsInWindow = AtomicLong(0L)

    // Statistics
    private val totalSent = AtomicLong(0L)
    private val sentObs = AtomicLong(0L)
    private val sentHb = AtomicLong(0L)
    private val rejectedCount = AtomicLong(0L)

    private var heartbeatJob: Job? = null
    private var reconnectJob: Job? = null

    fun connect(url: String = RswConstants.DEFAULT_CONTROLLER_URL, token: String = RswConstants.DEFAULT_TOKEN) {
        this.serverUrl = url
        this.authToken = token
        isRunning.set(true)

        updateTelemetry { copy(serverUrl = url, status = ConnectionStatus.CONNECTING, statusMessage = "Connecting to $url") }
        startWebSocket()
    }

    fun disconnect() {
        isRunning.set(false)
        heartbeatJob?.cancel()
        reconnectJob?.cancel()

        webSocket?.close(1000, "Client closed")
        webSocket = null

        resetSessionState()
        updateTelemetry {
            copy(
                status = ConnectionStatus.DISCONNECTED,
                statusMessage = "Disconnected",
                isClockSynced = false
            )
        }
    }

    private fun startWebSocket() {
        if (!isRunning.get()) return

        val request = Request.Builder()
            .url(serverUrl)
            .build()

        webSocket = okHttpClient.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) {
                Log.i(tag, "WebSocket opened, awaiting Welcome frame from controller...")
                updateTelemetry {
                    copy(
                        status = ConnectionStatus.AUTHENTICATING,
                        statusMessage = "Connected to AP, awaiting Welcome"
                    )
                }
            }

            override fun onMessage(ws: WebSocket, text: String) {
                handleTextMessage(text)
            }

            override fun onMessage(ws: WebSocket, bytes: ByteString) {
                // Per PHONE_PROTOCOL.md, controller sends JSON text only; client sends binary only
                Log.w(tag, "Unexpected binary frame received from controller (${bytes.size} bytes)")
            }

            override fun onClosing(ws: WebSocket, code: Int, reason: String) {
                Log.i(tag, "WebSocket closing: $code / $reason")
            }

            override fun onClosed(ws: WebSocket, code: Int, reason: String) {
                Log.i(tag, "WebSocket closed: $code / $reason")
                handleDisconnect()
            }

            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) {
                Log.e(tag, "WebSocket failure: ${t.message}")
                handleDisconnect()
            }
        })
    }

    private fun handleTextMessage(text: String) {
        when (val msg = RswServerMessage.parse(text)) {
            is RswServerMessage.Welcome -> {
                Log.i(tag, "Received Welcome: session=${msg.session}, version=${msg.version}")
                if (msg.version != 1) {
                    Log.e(tag, "Unsupported controller version: ${msg.version}")
                    disconnect()
                    return
                }

                // Sequence numbering must start fresh at 1 for AUTH
                sessionId = msg.session
                sequenceCounter.set(0L)
                authenticated = false
                clockSynchronizer.reset()
                lastFrontCaptureMs = 0L
                lastRearCaptureMs = 0L

                updateTelemetry {
                    copy(
                        sessionId = sessionId,
                        currentSequence = 0L,
                        status = ConnectionStatus.AUTHENTICATING,
                        statusMessage = "Welcome received, sending AUTH (seq=1)"
                    )
                }

                sendAuthPacket()
            }

            is RswServerMessage.SyncRequest -> {
                Log.d(tag, "Received SyncRequest: id=${msg.id}, controllerMs=${msg.controllerMs}")
                val phoneMs = clockSynchronizer.onSyncRequest(msg.controllerMs, msg.id)
                sendSyncPacket(msg.id, phoneMs)

                if (authenticated) {
                    updateTelemetry {
                        copy(
                            status = ConnectionStatus.CONNECTED_ACTIVE,
                            isClockSynced = true,
                            clockOffsetMs = clockSynchronizer.estimatedOffsetMs,
                            statusMessage = "Synchronized (Offset=${clockSynchronizer.estimatedOffsetMs}ms)"
                        )
                    }
                }
            }

            is RswServerMessage.Status -> {
                Log.d(tag, "Controller Status: state=${msg.state}, warning=${msg.warning}, front=${msg.frontValid}, rear=${msg.rearValid}")
                updateTelemetry {
                    copy(
                        controllerProfile = msg.profile,
                        controllerState = msg.state,
                        controllerSendState = msg.send,
                        controllerWarning = msg.warning,
                        controllerFrontValid = msg.frontValid,
                        controllerRearValid = msg.rearValid
                    )
                }
            }

            is RswServerMessage.Rejected -> {
                Log.w(tag, "Packet rejected by controller: code=${msg.code} (${msg.description})")
                rejectedCount.incrementAndGet()
                updateTelemetry {
                    copy(
                        rejectedPackets = rejectedCount.get(),
                        lastRejectCode = msg.rejectCode,
                        statusMessage = "Controller Rejected: ${msg.description}"
                    )
                }
            }

            is RswServerMessage.Unknown -> {
                Log.d(tag, "Unknown text message from controller: ${msg.rawJson}")
            }
        }
    }

    /**
     * Step 2 in protocol: Within 5s of Welcome, send binary AUTH with sequence 1.
     */
    private fun sendAuthPacket() {
        val seq = sequenceCounter.incrementAndGet() // sequence = 1
        require(seq == 1L) { "AUTH packet must have sequence 1" }

        val authPacket = RswPacket.createAuth(
            session = sessionId,
            sequence = seq,
            tokenString = authToken
        )

        sendBinaryPacket(authPacket)
        authenticated = true

        // Start 1 Hz heartbeat timer
        startHeartbeatLoop()
    }

    /**
     * Step 4 in protocol: Return binary SYNC with auxiliary = request ID and phone monotonic clock.
     */
    private fun sendSyncPacket(syncId: Long, phoneMonotonicMs: Long) {
        val seq = sequenceCounter.incrementAndGet()
        val syncPacket = RswPacket.createSync(
            session = sessionId,
            sequence = seq,
            syncId = syncId,
            phoneMonotonicMs = phoneMonotonicMs
        )
        sendBinaryPacket(syncPacket)
    }

    /**
     * Step 5 in protocol: Send binary HEARTBEAT every 1000 ms.
     */
    private fun startHeartbeatLoop() {
        heartbeatJob?.cancel()
        heartbeatJob = coroutineScope.launch {
            while (isActive && isRunning.get() && authenticated) {
                delay(RswConstants.HEARTBEAT_INTERVAL_MS)
                if (!isRunning.get() || !authenticated) break

                val nowMs = clockProvider()
                val seq = sequenceCounter.incrementAndGet()
                val hbPacket = RswPacket.createHeartbeat(
                    session = sessionId,
                    sequence = seq,
                    phoneMonotonicMs = nowMs
                )
                if (sendBinaryPacket(hbPacket)) {
                    sentHb.incrementAndGet()
                    updateTelemetry {
                        copy(
                            sentHeartbeats = sentHb.get(),
                            isClockSynced = clockSynchronizer.isClockValid()
                        )
                    }
                }
            }
        }
    }

    /**
     * Step 6 in protocol: Send OBSERVATION for each processed distinct frame.
     * Uses original capture timestamp in phone monotonic domain.
     * Enforces freshness bound: (now - captureMs) + uncertainty <= 500ms.
     */
    fun sendFrontObservation(
        capturePhoneMs: Long,
        cameraUncertaintyMs: Long,
        pothole: Boolean,
        wetRoad: Boolean,
        confidence: Int
    ): Boolean {
        if (!authenticated || sessionId == 0L) return false

        // Invariant: Capture timestamp must be strictly increasing per source (phone.cpp line 36)
        if (capturePhoneMs <= lastFrontCaptureMs) {
            Log.w(tag, "Front observation dropped: capture timestamp $capturePhoneMs <= last $lastFrontCaptureMs")
            return false
        }

        // Freshness check: reject if stale before sending to avoid wasting sequence numbers
        if (!clockSynchronizer.isFrameFresh(capturePhoneMs, cameraUncertaintyMs)) {
            Log.w(tag, "Front observation dropped: exceeds 500ms freshness bound")
            return false
        }

        val seq = sequenceCounter.incrementAndGet()
        val packet = RswPacket.createFrontObservation(
            session = sessionId,
            sequence = seq,
            capturePhoneMs = capturePhoneMs,
            cameraUncertaintyMs = cameraUncertaintyMs,
            pothole = pothole,
            wetRoad = wetRoad,
            confidence = confidence
        )

        val sent = sendBinaryPacket(packet)
        if (sent) {
            lastFrontCaptureMs = capturePhoneMs
            sentObs.incrementAndGet()
            updateTelemetry { copy(sentObservations = sentObs.get()) }
        }
        return sent
    }

    /**
     * Send Rear OBSERVATION packet.
     */
    fun sendRearObservation(
        capturePhoneMs: Long,
        cameraUncertaintyMs: Long,
        rearVehicle: Boolean,
        confidence: Int,
        zone: Byte = RswConstants.ZONE_UNKNOWN
    ): Boolean {
        if (!authenticated || sessionId == 0L) return false

        if (capturePhoneMs <= lastRearCaptureMs) {
            Log.w(tag, "Rear observation dropped: capture timestamp $capturePhoneMs <= last $lastRearCaptureMs")
            return false
        }

        if (!clockSynchronizer.isFrameFresh(capturePhoneMs, cameraUncertaintyMs)) {
            Log.w(tag, "Rear observation dropped: exceeds 500ms freshness bound")
            return false
        }

        val seq = sequenceCounter.incrementAndGet()
        val packet = RswPacket.createRearObservation(
            session = sessionId,
            sequence = seq,
            capturePhoneMs = capturePhoneMs,
            cameraUncertaintyMs = cameraUncertaintyMs,
            rearVehicle = rearVehicle,
            confidence = confidence,
            zone = zone
        )

        val sent = sendBinaryPacket(packet)
        if (sent) {
            lastRearCaptureMs = capturePhoneMs
            sentObs.incrementAndGet()
            updateTelemetry { copy(sentObservations = sentObs.get()) }
        }
        return sent
    }

    private fun sendBinaryPacket(packet: RswPacket): Boolean {
        val ws = webSocket ?: return false

        // Rate limiting check: max 40 packets per second
        val now = clockProvider()
        val windowStart = rateWindowStartMs.get()
        if (now - windowStart >= 1000L) {
            rateWindowStartMs.set(now)
            packetsInWindow.set(0L)
        }
        if (packetsInWindow.incrementAndGet() > 40L) {
            Log.w(tag, "Rate limiter engaged: suppressing packet to prevent controller disconnect")
            return false
        }

        val encoded = RswCodec.encode(packet)
        val success = ws.send(encoded.toByteString())
        if (success) {
            totalSent.incrementAndGet()
            updateTelemetry {
                copy(
                    totalSentPackets = totalSent.get(),
                    currentSequence = packet.sequence
                )
            }
        } else {
            Log.e(tag, "WebSocket send failed for packet type ${packet.type}")
        }
        return success
    }

    private fun handleDisconnect() {
        heartbeatJob?.cancel()
        resetSessionState()

        updateTelemetry {
            copy(
                status = ConnectionStatus.RECONNECTING,
                statusMessage = "Connection lost. Reconnecting...",
                isClockSynced = false
            )
        }

        // Exponential backoff reconnect
        if (isRunning.get()) {
            reconnectJob?.cancel()
            reconnectJob = coroutineScope.launch {
                delay(2000L)
                if (isRunning.get()) {
                    Log.i(tag, "Attempting reconnect to $serverUrl...")
                    startWebSocket()
                }
            }
        }
    }

    private fun resetSessionState() {
        authenticated = false
        sessionId = 0L
        sequenceCounter.set(0L)
        clockSynchronizer.reset()
        lastFrontCaptureMs = 0L
        lastRearCaptureMs = 0L
    }

    private inline fun updateTelemetry(update: RswClientTelemetry.() -> RswClientTelemetry) {
        _telemetry.value = _telemetry.value.update()
    }
}

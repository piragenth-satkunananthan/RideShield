package com.rideshield.phone.network

import android.os.SystemClock
import com.rideshield.phone.protocol.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import okhttp3.*
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.util.concurrent.TimeUnit

/** Session changes, sequence allocation and sends share one monitor. */
class RswWebSocketClient(
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() },
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.IO + SupervisorJob()),
    private val socketFactory: WebSocket.Factory = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS).writeTimeout(5, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.SECONDS).build()
) {
    val clockSynchronizer = RswClockSynchronizer(clockProvider)
    private val state = MutableStateFlow(RswClientTelemetry())
    val telemetry = state.asStateFlow()
    private var socket: WebSocket? = null
    private var running = false
    private var generation = 0L
    private var session = 0L
    private var sequence = 0L
    private var authenticated = false
    private var lastFront = -1L
    private var lastRear = -1L
    private var connectedAt = 0L
    private var lastStatus = -1L
    private var lastHeartbeat = 0L
    private var rateAt = 0L
    private var rateCount = 0
    private var retryMs = 1000L
    private var timer: Job? = null
    private var reconnect: Job? = null
    private var url = RswConstants.DEFAULT_CONTROLLER_URL
    private var token = RswConstants.DEFAULT_TOKEN

    @Synchronized fun connect(url: String = RswConstants.DEFAULT_CONTROLLER_URL, token: String = RswConstants.DEFAULT_TOKEN) {
        disconnect()
        if (!validSettings(url, token)) {
            state.value = state.value.copy(status = ConnectionStatus.ERROR, statusMessage = "Use a ws/wss URL and exactly 32 ASCII token characters")
            return
        }
        this.url = url; this.token = token; running = true; retryMs = 1000
        state.value = RswClientTelemetry(serverUrl = url, status = ConnectionStatus.CONNECTING, statusMessage = "Connecting to controller")
        open()
    }
    @Synchronized fun disconnect() {
        running = false; generation++
        timer?.cancel(); reconnect?.cancel(); socket?.cancel(); socket = null
        resetSession()
        state.value = RswClientTelemetry(serverUrl = url, statusMessage = "Disconnected")
    }
    @Synchronized private fun open() {
        if (!running) return
        val attempt = ++generation
        resetSession(); connectedAt = clockProvider()
        socket = socketFactory.newWebSocket(Request.Builder().url(url).build(), object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) = synchronized(this@RswWebSocketClient) {
                if (attempt != generation || !running) { ws.cancel(); return@synchronized }
                state.value = state.value.copy(status = ConnectionStatus.AUTHENTICATING, statusMessage = "Waiting for controller welcome")
            }
            override fun onMessage(ws: WebSocket, text: String) = synchronized(this@RswWebSocketClient) {
                if (attempt == generation && running) receive(text)
            }
            override fun onMessage(ws: WebSocket, bytes: ByteString) = synchronized(this@RswWebSocketClient) {
                if (attempt == generation && running) lost("Unexpected binary controller message")
            }
            override fun onClosing(ws: WebSocket, code: Int, reason: String) = synchronized(this@RswWebSocketClient) {
                if (attempt == generation && running) lost("Controller closed connection")
            }
            override fun onClosed(ws: WebSocket, code: Int, reason: String) = synchronized(this@RswWebSocketClient) {
                if (attempt == generation && running) lost("Controller disconnected")
            }
            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) = synchronized(this@RswWebSocketClient) {
                if (attempt == generation && running) lost("Connection failed; check helmet Wi-Fi and token")
            }
        })
        timer?.cancel()
        timer = coroutineScope.launch {
            while (isActive) {
                delay(250)
                synchronized(this@RswWebSocketClient) { if (attempt == generation && running) tick() }
            }
        }
    }
    private fun receive(text: String) {
        when (val msg = RswServerMessage.parse(text)) {
            is RswServerMessage.Welcome -> {
                if (session != 0L || msg.version != 1 || msg.session !in 1..0xffffffffL) { lost("Invalid welcome"); return }
                session = msg.session; state.value = state.value.copy(sessionId = session)
                send(RswPacket.createAuth(session, ++sequence, token))
                // AUTH is confirmed by the controller's first sync request, not by socket.send().
            }
            is RswServerMessage.SyncRequest -> {
                if (session == 0L || sequence == 0L || msg.id !in 1..0xffffffffL || msg.controllerMs < 0) { lost("Unexpected sync"); return }
                val now = clockSynchronizer.onSyncRequest(msg.controllerMs, msg.id)
                if (!send(RswPacket.createSync(session, ++sequence, msg.id, now))) return
                authenticated = true
                state.value = state.value.copy(status = if (lastStatus >= 0) ConnectionStatus.CONNECTED_ACTIVE else ConnectionStatus.SYNCING,
                    clockOffsetMs = clockSynchronizer.estimatedOffsetMs, clockUncertaintyMs = clockSynchronizer.uncertaintyMs,
                    statusMessage = "Authenticated; waiting for controller status")
            }
            is RswServerMessage.Status -> {
                if (!authenticated) return
                lastStatus = clockProvider(); retryMs = 1000
                state.value = state.value.copy(status = ConnectionStatus.CONNECTED_ACTIVE, isClockSynced = clockSynchronizer.isClockValid(),
                    controllerProfile = msg.profile, controllerState = msg.state, controllerSendState = msg.send,
                    controllerWarning = msg.warning, controllerFrontValid = msg.frontValid, controllerRearValid = msg.rearValid,
                    statusMessage = "Controller connected")
            }
            is RswServerMessage.Rejected -> {
                state.value = state.value.copy(rejectedPackets = state.value.rejectedPackets + 1, lastRejectCode = msg.rejectCode,
                    statusMessage = "Rejected: ${msg.rejectCode.name}")
                if (msg.rejectCode in listOf(RswRejectCode.SESSION, RswRejectCode.SEQUENCE, RswRejectCode.CLOCK, RswRejectCode.OFFLINE)) lost("Session rejected; reconnecting")
            }
            is RswServerMessage.Unknown -> Unit
        }
    }
    private fun tick() {
        val now = clockProvider()
        if ((lastStatus < 0 && now - connectedAt > 6000) || (lastStatus >= 0 && now - lastStatus > 3000)) { lost("Controller status timed out"); return }
        if (authenticated && now - lastHeartbeat >= 1000) {
            lastHeartbeat = now
            if (send(RswPacket.createHeartbeat(session, ++sequence, now)))
                state.value = state.value.copy(sentHeartbeats = state.value.sentHeartbeats + 1, isClockSynced = clockSynchronizer.isClockValid())
        }
    }
    @Synchronized fun sendFrontObservation(capturePhoneMs: Long, cameraUncertaintyMs: Long, pothole: Boolean, wetRoad: Boolean, confidence: Int): Boolean {
        if (!canObserve(capturePhoneMs, cameraUncertaintyMs, lastFront, confidence)) return false
        if (!send(RswPacket.createFrontObservation(session, ++sequence, capturePhoneMs, cameraUncertaintyMs, pothole, wetRoad, confidence))) return false
        lastFront = capturePhoneMs; state.value = state.value.copy(sentObservations = state.value.sentObservations + 1); return true
    }
    @Synchronized fun sendRearObservation(capturePhoneMs: Long, cameraUncertaintyMs: Long, rearVehicle: Boolean, confidence: Int, zone: Byte = RswConstants.ZONE_UNKNOWN): Boolean {
        if (zone !in 0..3 || !canObserve(capturePhoneMs, cameraUncertaintyMs, lastRear, confidence)) return false
        if (!send(RswPacket.createRearObservation(session, ++sequence, capturePhoneMs, cameraUncertaintyMs, rearVehicle, confidence, zone))) return false
        lastRear = capturePhoneMs; state.value = state.value.copy(sentObservations = state.value.sentObservations + 1); return true
    }
    private fun canObserve(capture: Long, uncertainty: Long, previous: Long, confidence: Int): Boolean =
        running && authenticated && lastStatus >= 0 && clockProvider() - lastStatus in 0..3000 && capture > previous &&
            confidence in 0..100 && clockSynchronizer.isFrameFresh(capture, uncertainty)
    private fun send(packet: RswPacket): Boolean {
        val ws = socket ?: return false
        val now = clockProvider()
        if (now - rateAt >= 1000) { rateAt = now; rateCount = 0 }
        if (packet.type == RswConstants.TYPE_OBSERVATION && rateCount >= 35) return false // reserve control capacity
        if (rateCount >= 40) { lost("Packet budget exceeded"); return false }
        rateCount++
        if (!ws.send(RswCodec.encode(packet).toByteString())) { lost("Socket send failed"); return false }
        state.value = state.value.copy(totalSentPackets = state.value.totalSentPackets + 1, currentSequence = packet.sequence)
        return true
    }
    private fun lost(message: String) {
        generation++; timer?.cancel(); socket?.cancel(); socket = null; resetSession()
        state.value = RswClientTelemetry(serverUrl = url, status = if (running) ConnectionStatus.RECONNECTING else ConnectionStatus.DISCONNECTED, statusMessage = message)
        if (!running) return
        val attempt = generation; val wait = retryMs; retryMs = (retryMs * 2).coerceAtMost(15000)
        reconnect?.cancel()
        reconnect = coroutineScope.launch { delay(wait); synchronized(this@RswWebSocketClient) { if (running && generation == attempt) open() } }
    }
    private fun resetSession() {
        session = 0; sequence = 0; authenticated = false; lastFront = -1; lastRear = -1
        lastStatus = -1; lastHeartbeat = 0; rateAt = clockProvider(); rateCount = 0; clockSynchronizer.reset()
    }
    companion object {
        fun validSettings(url: String, token: String): Boolean = token.length == 32 && token.all { it.code in 32..126 } &&
            (url.startsWith("ws://") || url.startsWith("wss://")) &&
            runCatching { Request.Builder().url(url).build().url.let { it.username.isEmpty() && it.password.isEmpty() } }.getOrDefault(false)
    }
}

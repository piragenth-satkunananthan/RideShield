package com.rideshield.phone.camera

import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

enum class CameraConnectionState {
    STOPPED,
    CONNECTING,
    STREAMING,
    ERROR
}

/**
 * Camera Stream Client for ESP32-S3 Camera Nodes.
 * Ingests timestamped multipart MJPEG streams, decodes custom headers,
 * maps camera clock to phone clock, feeds bounded newest-frame queue,
 * and reports stream quality statistics.
 */
class CameraStreamClient(
    val nodeName: String,
    val boundedQueue: BoundedFrameQueue = BoundedFrameQueue(capacity = 1),
    val clockFilter: CameraClockFilter = CameraClockFilter(),
    val qualityMonitor: CameraQualityMonitor = CameraQualityMonitor(nodeName),
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
) {
    private val tag = "CameraStreamClient-$nodeName"

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()

    private val mockSource = MockCameraSource(nodeName)
    private val isRunning = AtomicBoolean(false)
    private var streamJob: Job? = null

    private val _connectionState = MutableStateFlow(CameraConnectionState.STOPPED)
    val connectionState: StateFlow<CameraConnectionState> = _connectionState.asStateFlow()

    private val _stats = MutableStateFlow(qualityMonitor.getSnapshot(boundedQueue.totalDropped))
    val stats: StateFlow<StreamQualityStats> = _stats.asStateFlow()

    @Volatile var isMockMode: Boolean = false
    @Volatile var streamUrl: String = ""

    fun start(url: String, useMock: Boolean = false) {
        this.streamUrl = url
        this.isMockMode = useMock
        isRunning.set(true)

        streamJob?.cancel()
        streamJob = coroutineScope.launch {
            if (isMockMode) {
                runMockLoop()
            } else {
                runHttpStreamLoop()
            }
        }
    }

    fun stop() {
        isRunning.set(false)
        streamJob?.cancel()
        streamJob = null
        boundedQueue.clear()
        _connectionState.value = CameraConnectionState.STOPPED
    }

    private suspend fun runMockLoop() {
        Log.i(tag, "Starting simulated camera feed for node $nodeName")
        _connectionState.value = CameraConnectionState.STREAMING

        var counter = 0
        while (coroutineScope.isActive && isRunning.get()) {
            val hasHazard = (counter % 30 in 10..18) // Periodically simulate a hazard for bench demo
            val frame = mockSource.generateMockFrame(hasHazard)

            boundedQueue.push(frame)
            qualityMonitor.onFrameReceived(frame)
            _stats.value = qualityMonitor.getSnapshot(boundedQueue.totalDropped)

            counter++
            delay(100L) // 10 FPS simulated
        }
    }

    private suspend fun runHttpStreamLoop() {
        while (coroutineScope.isActive && isRunning.get()) {
            _connectionState.value = CameraConnectionState.CONNECTING
            Log.i(tag, "Connecting to camera stream: $streamUrl")

            try {
                val request = Request.Builder().url(streamUrl).build()
                val response = httpClient.newCall(request).execute()

                if (!response.isSuccessful) {
                    Log.w(tag, "Camera HTTP response error: ${response.code}")
                    _connectionState.value = CameraConnectionState.ERROR
                    delay(2000L)
                    continue
                }

                _connectionState.value = CameraConnectionState.STREAMING
                val inputStream = BufferedInputStream(response.body?.byteStream() ?: throw IllegalStateException("Empty body"))
                readMjpegStream(inputStream)

            } catch (e: Exception) {
                if (isRunning.get()) {
                    Log.e(tag, "Camera stream network error: ${e.message}")
                    _connectionState.value = CameraConnectionState.ERROR
                    delay(2000L)
                }
            }
        }
    }

    /**
     * Reads multipart MJPEG stream and extracts custom X- headers for timestamps.
     */
    private fun readMjpegStream(inputStream: BufferedInputStream) {
        var frameIndex = 0L
        val buffer = ByteArray(8192)

        while (isRunning.get()) {
            val headers = readHeaders(inputStream) ?: break
            val contentLength = headers["content-length"]?.toIntOrNull() ?: 0
            val headerFrameIdx = headers["x-frame-index"]?.toLongOrNull() ?: ++frameIndex
            val headerTimestampUs = headers["x-capture-timestamp-us"]?.toLongOrNull()
                ?: (SystemClock.elapsedRealtime() * 1000L)
            val exposureUs = headers["x-exposure-us"]?.toLongOrNull() ?: 0L
            val gain = headers["x-sensor-gain"]?.toIntOrNull() ?: 0

            val jpegBytes = if (contentLength > 0) {
                readExactBytes(inputStream, contentLength)
            } else {
                readUntilBoundary(inputStream)
            }

            if (jpegBytes.isEmpty()) continue

            val arrivalMs = SystemClock.elapsedRealtime()
            val (mappedCaptureMs, uncertaintyMs) = clockFilter.processFrameTimestamp(headerTimestampUs, arrivalMs)

            val frame = CameraFrame(
                cameraNode = nodeName,
                frameIndex = headerFrameIdx,
                cameraCaptureTimestampUs = headerTimestampUs,
                jpegBytes = jpegBytes,
                phoneArrivalMs = arrivalMs,
                mappedPhoneCaptureMs = mappedCaptureMs,
                clockUncertaintyMs = uncertaintyMs,
                exposureUs = exposureUs,
                sensorGain = gain
            )

            boundedQueue.push(frame)
            qualityMonitor.onFrameReceived(frame)
            _stats.value = qualityMonitor.getSnapshot(boundedQueue.totalDropped)
        }
    }

    private fun readHeaders(stream: BufferedInputStream): Map<String, String>? {
        val headers = mutableMapOf<String, String>()
        val line = StringBuilder()

        while (true) {
            val c = stream.read()
            if (c == -1) return null
            if (c == '\n'.code) {
                val str = line.toString().trim()
                if (str.isEmpty()) break // End of HTTP headers
                val colon = str.indexOf(':')
                if (colon > 0) {
                    val key = str.substring(0, colon).trim().lowercase()
                    val value = str.substring(colon + 1).trim()
                    headers[key] = value
                }
                line.clear()
            } else if (c != '\r'.code) {
                line.append(c.toChar())
            }
        }
        return headers
    }

    private fun readExactBytes(stream: BufferedInputStream, size: Int): ByteArray {
        val bytes = ByteArray(size)
        var read = 0
        while (read < size) {
            val n = stream.read(bytes, read, size - read)
            if (n == -1) break
            read += n
        }
        return bytes
    }

    private fun readUntilBoundary(stream: BufferedInputStream): ByteArray {
        val out = ByteArrayOutputStream()
        var prev = 0
        while (true) {
            val b = stream.read()
            if (b == -1) break
            out.write(b)
            // Check for JPEG EOI (0xFF 0xD9)
            if (prev == 0xFF && b == 0xD9) {
                break
            }
            prev = b
        }
        return out.toByteArray()
    }
}

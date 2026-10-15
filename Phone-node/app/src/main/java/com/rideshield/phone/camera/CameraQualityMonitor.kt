package com.rideshield.phone.camera

import android.graphics.Bitmap
import android.os.SystemClock
import java.util.concurrent.atomic.AtomicLong
import kotlin.math.max

data class StreamQualityStats(
    val nodeName: String = "CAMERA",
    val fps: Double = 0.0,
    val streamAgeMs: Long = 0L,
    val sequenceGaps: Long = 0L,
    val totalReceivedFrames: Long = 0L,
    val droppedFrames: Long = 0L,
    val averageBytesPerFrame: Long = 0L,
    val decodeLatencyMs: Long = 0L,
    val estimatedSharpnessScore: Double = 0.0,
    val isStreamHealthy: Boolean = false,
    val lastFrameWidth: Int = 0,
    val lastFrameHeight: Int = 0
)

/**
 * Monitors incoming camera stream health, detects frame gaps, measures stream age,
 * and computes image quality indicators.
 */
class CameraQualityMonitor(
    val nodeName: String,
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() }
) {
    private val totalFrames = AtomicLong(0L)
    private val frameGaps = AtomicLong(0L)
    private var lastFrameIndex: Long = -1L

    // FPS tracking
    private var fpsWindowStart = clockProvider()
    private var framesInWindow = 0
    @Volatile var currentFps: Double = 0.0
        private set

    @Volatile var latestAgeMs: Long = 0L
        private set
    @Volatile var latestDecodeLatencyMs: Long = 0L
        private set
    @Volatile var latestByteSize: Long = 0L
        private set
    @Volatile var latestSharpnessScore: Double = 0.0
        private set
    @Volatile var lastWidth: Int = 0
        private set
    @Volatile var lastHeight: Int = 0
        private set

    @Synchronized
    fun onFrameReceived(frame: CameraFrame) {
        val count = totalFrames.incrementAndGet()

        // Detect frame index gaps from camera MCU
        if (lastFrameIndex >= 0 && frame.frameIndex > lastFrameIndex + 1) {
            val missed = frame.frameIndex - lastFrameIndex - 1
            frameGaps.addAndGet(missed)
        }
        lastFrameIndex = frame.frameIndex

        // Calculate stream age (time from original capture to now)
        val now = clockProvider()
        latestAgeMs = max(0L, now - frame.mappedPhoneCaptureMs)
        latestByteSize = frame.jpegBytes.size.toLong()

        // Calculate FPS
        framesInWindow++
        val windowDuration = now - fpsWindowStart
        if (windowDuration >= 1000L) {
            currentFps = (framesInWindow * 1000.0) / windowDuration
            framesInWindow = 0
            fpsWindowStart = now
        }
    }

    @Synchronized
    fun onFrameDecoded(bitmap: Bitmap?, decodeDurationMs: Long) {
        latestDecodeLatencyMs = decodeDurationMs
        if (bitmap != null) {
            lastWidth = bitmap.width
            lastHeight = bitmap.height
            latestSharpnessScore = estimateQuickSharpness(bitmap)
        }
    }

    /**
     * Fast estimation of image sharpness using high-frequency edge sampling.
     */
    private fun estimateQuickSharpness(bitmap: Bitmap): Double {
        return try {
            val w = bitmap.width
            val h = bitmap.height
            if (w < 10 || h < 10) return 0.0

            // Sample a 32x32 grid across the center of the frame
            val stepX = max(1, w / 32)
            val stepY = max(1, h / 32)
            var gradSum = 0.0
            var samples = 0

            for (y in stepY until h - stepY step stepY) {
                for (x in stepX until w - stepX step stepX) {
                    val p = bitmap.getPixel(x, y)
                    val pRight = bitmap.getPixel(x + 1, y)
                    val pDown = bitmap.getPixel(x, y + 1)

                    val gray = (p and 0xFF)
                    val grayR = (pRight and 0xFF)
                    val grayD = (pDown and 0xFF)

                    gradSum += kotlin.math.abs(gray - grayR) + kotlin.math.abs(gray - grayD)
                    samples++
                }
            }
            if (samples > 0) gradSum / samples else 0.0
        } catch (e: Exception) {
            0.0
        }
    }

    fun getSnapshot(droppedFrames: Long): StreamQualityStats {
        val now = clockProvider()
        val healthy = currentFps > 5.0 && latestAgeMs < 400L

        return StreamQualityStats(
            nodeName = nodeName,
            fps = currentFps,
            streamAgeMs = latestAgeMs,
            sequenceGaps = frameGaps.get(),
            totalReceivedFrames = totalFrames.get(),
            droppedFrames = droppedFrames,
            averageBytesPerFrame = latestByteSize,
            decodeLatencyMs = latestDecodeLatencyMs,
            estimatedSharpnessScore = latestSharpnessScore,
            isStreamHealthy = healthy,
            lastFrameWidth = lastWidth,
            lastFrameHeight = lastHeight
        )
    }

    fun reset() {
        totalFrames.set(0L)
        frameGaps.set(0L)
        lastFrameIndex = -1L
        currentFps = 0.0
        latestAgeMs = 0L
        latestDecodeLatencyMs = 0L
        latestSharpnessScore = 0.0
    }
}

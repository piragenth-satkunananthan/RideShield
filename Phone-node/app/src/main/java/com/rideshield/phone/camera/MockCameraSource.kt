package com.rideshield.phone.camera

import android.graphics.*
import android.os.SystemClock
import java.io.ByteArrayOutputStream
import java.util.concurrent.atomic.AtomicLong
import kotlin.random.Random

/**
 * Generates synthetic timestamped camera frames for bench testing and offline development
 * when physical ESP32-S3 camera nodes are not powered or available.
 */
class MockCameraSource(
    val nodeName: String = "FRONT",
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() }
) {
    private val frameIndexCounter = AtomicLong(0L)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 28f
        typeface = Typeface.MONOSPACE
    }

    /**
     * Generates a realistic mock JPEG frame with simulated road scene, asphalt texture,
     * optional synthetic potholes (for FRONT) or vehicle silhouettes (for REAR),
     * and burns in timestamp and sequence numbers.
     */
    fun generateMockFrame(hasHazard: Boolean = false): CameraFrame {
        val width = 640
        val height = 480
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)

        // Draw simulated road / asphalt background
        paint.color = Color.rgb(45, 48, 55)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)

        // Draw perspective road lanes
        paint.color = Color.rgb(200, 200, 200)
        paint.strokeWidth = 6f
        canvas.drawLine(width * 0.15f, height.toFloat(), width * 0.42f, height * 0.45f, paint)
        canvas.drawLine(width * 0.85f, height.toFloat(), width * 0.58f, height * 0.45f, paint)

        // Dashed center line
        paint.color = Color.rgb(240, 200, 50)
        paint.strokeWidth = 4f
        var y = height * 0.45f
        while (y < height) {
            canvas.drawLine(width * 0.5f, y, width * 0.5f, y + 25f, paint)
            y += 50f
        }

        val frameIdx = frameIndexCounter.incrementAndGet()
        val nowPhoneMs = clockProvider()
        // Simulate camera hardware microsecond counter (e.g. running for some hours)
        val simCameraUs = (nowPhoneMs * 1000L) + 1234567L

        if (nodeName == "FRONT" && hasHazard) {
            // Draw a synthetic pothole
            paint.color = Color.rgb(18, 18, 20)
            val potholeRect = RectF(width * 0.45f, height * 0.65f, width * 0.62f, height * 0.78f)
            canvas.drawOval(potholeRect, paint)

            paint.color = Color.rgb(80, 80, 85)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 3f
            canvas.drawOval(potholeRect, paint)
            paint.style = Paint.Style.FILL
        } else if (nodeName == "REAR" && hasHazard) {
            // Draw a synthetic vehicle approaching from rear
            paint.color = Color.rgb(180, 30, 30)
            val carRect = RectF(width * 0.38f, height * 0.48f, width * 0.62f, height * 0.72f)
            canvas.drawRoundRect(carRect, 16f, 16f, paint)
            // Headlights
            paint.color = Color.rgb(255, 255, 220)
            canvas.drawCircle(width * 0.42f, height * 0.64f, 14f, paint)
            canvas.drawCircle(width * 0.58f, height * 0.64f, 14f, paint)
        }

        // Burn-in HUD diagnostics on mock frame
        canvas.drawText("NODE: $nodeName (SIMULATED)", 20f, 40f, textPaint)
        canvas.drawText("FRAME #$frameIdx | CAPTURE: ${nowPhoneMs}ms", 20f, 80f, textPaint)

        // Compress to JPEG
        val baos = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.JPEG, 80, baos)
        val jpegBytes = baos.toByteArray()
        bitmap.recycle()

        return CameraFrame(
            synthetic = true,
            cameraNode = nodeName,
            frameIndex = frameIdx,
            cameraCaptureTimestampUs = simCameraUs,
            jpegBytes = jpegBytes,
            phoneArrivalMs = nowPhoneMs,
            mappedPhoneCaptureMs = nowPhoneMs, // in mock, capture time equals current time
            clockUncertaintyMs = 15L,
            exposureUs = 12000L,
            sensorGain = 100,
            width = width,
            height = height
        )
    }

    fun reset() {
        frameIndexCounter.set(0L)
    }
}

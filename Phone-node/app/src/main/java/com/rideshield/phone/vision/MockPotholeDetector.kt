package com.rideshield.phone.vision

import android.graphics.Bitmap
import android.graphics.RectF
import android.os.SystemClock

/**
 * Fallback / Mock pothole detector used for bench simulation and JVM unit testing.
 */
class MockPotholeDetector(
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() }
) : PotholeDetector {

    @Volatile var isSimulateHazard: Boolean = false

    override fun initialize(): Boolean = true

    override fun detect(frameBitmap: Bitmap): PotholeDetectionResult {
        val start = clockProvider()

        // Quick heuristic or simulation mode
        val detected = mutableListOf<BoundingBox>()
        if (isSimulateHazard) {
            val w = frameBitmap.width.toFloat()
            val h = frameBitmap.height.toFloat()
            val rect = RectF(w * 0.42f, h * 0.65f, w * 0.62f, h * 0.82f)
            detected.add(BoundingBox(rect, 0.88f, "Pothole"))
        }

        val duration = clockProvider() - start
        val highestConf = if (detected.isNotEmpty()) (detected.maxOf { it.confidence } * 100).toInt() else 0
        val isHazard = highestConf >= 60

        return PotholeDetectionResult(
            potholes = detected,
            highestConfidence = highestConf,
            inferenceDurationMs = duration,
            isHazardPresent = isHazard,
            status = ModelStatus.LOADED
        )
    }

    override fun getModelInfo(): ModelInfo {
        return ModelInfo(
            name = "Pothole Detector (Simulation Mode)",
            version = "YOLOv12s-Mock-1.0",
            status = ModelStatus.LOADED,
            description = "Simulated detector for bench testing without native ONNX weights."
        )
    }

    override fun close() {}
}

package com.rideshield.phone.vision
import android.graphics.Bitmap
/** Failed model loading must never select a synthetic detector. */
class UnavailablePotholeDetector : PotholeDetector {
    override fun initialize() = false
    override fun detect(frameBitmap: Bitmap) = PotholeDetectionResult(status = ModelStatus.UNAVAILABLE)
    override fun getModelInfo() = ModelInfo("Pothole", "UNAVAILABLE", ModelStatus.UNAVAILABLE, "Model failed to load; no observations sent")
    override fun close() = Unit
}

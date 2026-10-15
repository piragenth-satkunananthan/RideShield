package com.rideshield.phone.vision

/**
 * Implementation of WetRoadClassifier when no trained wet-road model weights are loaded.
 * Explicitly reports status UNAVAILABLE and NEVER generates invented or synthetic detections,
 * maintaining safety integrity per system requirements.
 */
class UnavailableWetRoadClassifier : WetRoadClassifier {

    override fun initialize(): Boolean {
        // Model weights are not provided in this release
        return false
    }

    override fun classify(frameBitmap: android.graphics.Bitmap): WetRoadResult {
        // Never invent detections: strictly report no hazard and UNAVAILABLE status
        return WetRoadResult(
            isWetRoad = false,
            confidence = 0,
            inferenceDurationMs = 0L,
            status = ModelStatus.UNAVAILABLE
        )
    }

    override fun getModelInfo(): ModelInfo {
        return ModelInfo(
            name = "Wet-Road Friction Classifier",
            version = "UNAVAILABLE",
            status = ModelStatus.UNAVAILABLE,
            description = "RGB wet-surface model weights not installed on device."
        )
    }

    override fun close() {
        // No resources to release
    }
}

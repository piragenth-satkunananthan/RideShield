package com.rideshield.phone.vision

import com.rideshield.phone.protocol.RswConstants

/**
 * Implementation of RearVehicleDetectorTracker when no validated rear-camera model weights are loaded.
 * Explicitly reports status UNAVAILABLE and NEVER generates invented detections or synthetic tracks.
 *
 * Notice per PHONE_PROTOCOL.md line 94:
 * "Rear-vehicle is a general advisory. Camera image position alone is not a
 * bike-relative blind spot, especially on a moving helmet. No numeric distance,
 * closing speed or time-to-collision is carried by this version. Do not label the
 * simulator as validated blind-spot detection."
 */
class UnavailableRearVehicleDetector : RearVehicleDetectorTracker {

    override fun initialize(): Boolean {
        // Model weights and tracking pipeline not installed in this release
        return false
    }

    override fun detectAndTrack(frameBitmap: android.graphics.Bitmap?): RearVehicleResult {
        // Never invent detections or spoof tracks
        return RearVehicleResult(
            detectedVehicles = emptyList(),
            highestConfidence = 0,
            isHazardPresent = false,
            zone = RswConstants.ZONE_UNKNOWN,
            inferenceDurationMs = 0L,
            status = ModelStatus.UNAVAILABLE
        )
    }

    override fun getModelInfo(): ModelInfo {
        return ModelInfo(
            name = "Rear Vehicle Detector & Tracker",
            version = "UNAVAILABLE",
            status = ModelStatus.UNAVAILABLE,
            description = "Rear vehicle vision & tracking model weights not installed on device."
        )
    }

    override fun close() {
        // No resources to release
    }
}

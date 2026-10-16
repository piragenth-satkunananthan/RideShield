package com.rideshield.phone.vision

import android.graphics.RectF
import com.rideshield.phone.protocol.RswConstants

enum class ModelStatus {
    LOADED,
    UNAVAILABLE,
    ERROR
}

data class ModelInfo(
    val name: String,
    val version: String,
    val status: ModelStatus,
    val description: String
)

data class BoundingBox(
    val rect: RectF,
    val confidence: Float,
    val label: String
)

data class PotholeDetectionResult(
    val potholes: List<BoundingBox> = emptyList(),
    val highestConfidence: Int = 0,
    val inferenceDurationMs: Long = 0L,
    val isHazardPresent: Boolean = false,
    val status: ModelStatus = ModelStatus.LOADED
)

data class WetRoadResult(
    val isWetRoad: Boolean = false,
    val confidence: Int = 0,
    val inferenceDurationMs: Long = 0L,
    val status: ModelStatus = ModelStatus.UNAVAILABLE
)

data class RearVehicleResult(
    val detectedVehicles: List<BoundingBox> = emptyList(),
    val highestConfidence: Int = 0,
    val isHazardPresent: Boolean = false,
    val zone: Byte = RswConstants.ZONE_UNKNOWN,
    val inferenceDurationMs: Long = 0L,
    val status: ModelStatus = ModelStatus.UNAVAILABLE
)

interface PotholeDetector {
    fun initialize(): Boolean
    fun detect(frameBitmap: android.graphics.Bitmap): PotholeDetectionResult
    fun getModelInfo(): ModelInfo
    fun close()
}

interface WetRoadClassifier {
    fun initialize(): Boolean
    fun classify(frameBitmap: android.graphics.Bitmap?): WetRoadResult
    fun getModelInfo(): ModelInfo
    fun close()
}

interface RearVehicleDetectorTracker {
    fun initialize(): Boolean
    fun detectAndTrack(frameBitmap: android.graphics.Bitmap?): RearVehicleResult
    fun getModelInfo(): ModelInfo
    fun close()
}

package com.rideshield.phone.camera

import android.graphics.Bitmap
import android.graphics.BitmapFactory

/**
 * Represents a timestamped camera frame received from an ESP32-S3 camera node.
 */
data class CameraFrame(
    val cameraNode: String,                 // "FRONT" or "REAR"
    val frameIndex: Long,                   // Strictly increasing frame sequence from camera MCU
    val cameraCaptureTimestampUs: Long,     // Microsecond hardware timestamp (esp_timer_get_time)
    val jpegBytes: ByteArray,               // Encoded JPEG payload
    val phoneArrivalMs: Long,               // Local phone monotonic time when frame arrived over network
    val mappedPhoneCaptureMs: Long,         // Estimated original capture time in phone monotonic domain
    val clockUncertaintyMs: Long,           // Camera-to-phone clock uncertainty in ms (auxiliary field)
    val exposureUs: Long = 0L,              // Sensor exposure duration in microseconds
    val sensorGain: Int = 0,                // Sensor analog/digital gain
    val width: Int = 0,
    val synthetic: Boolean = false,
    val height: Int = 0
) {
    /**
     * Decodes JPEG payload to an Android Bitmap.
     */
    fun decodeBitmap(): Bitmap? {
        return try {
            BitmapFactory.decodeByteArray(jpegBytes, 0, jpegBytes.size)
        } catch (e: Exception) {
            null
        }
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as CameraFrame
        return frameIndex == other.frameIndex && cameraNode == other.cameraNode
    }

    override fun hashCode(): Int {
        var result = cameraNode.hashCode()
        result = 31 * result + frameIndex.hashCode()
        return result
    }
}

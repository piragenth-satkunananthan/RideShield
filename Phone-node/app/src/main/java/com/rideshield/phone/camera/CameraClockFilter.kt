package com.rideshield.phone.camera

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * Filters camera MCU hardware timestamps and maps them into the phone's monotonic time domain.
 * Estimates clock offset, bounds clock uncertainty, and validates clock freshness.
 */
class CameraClockFilter(
    private val clockProvider: () -> Long = { android.os.SystemClock.elapsedRealtime() }
) {
    @Volatile var isClockKnown: Boolean = false
        private set

    @Volatile var filteredOffsetMs: Long = 0L
        private set

    @Volatile var clockUncertaintyMs: Long = 50L
        private set

    private var sampleCount: Int = 0
    private var minObservedDelayMs: Long = Long.MAX_VALUE
    private var lastFrameCameraUs: Long = 0L
    private var lastPhoneArrivalMs: Long = 0L
    private var explicitSyncAtMs: Long? = null
    private var explicitSyncUncertaintyMs: Long = 0L

    /**
     * Updates clock filter with an incoming frame's arrival time and camera hardware timestamp.
     * @param cameraUs Hardware timestamp in microseconds from camera ESP32 esp_timer_get_time()
     * @param phoneArrivalMs Local monotonic arrival time on phone in milliseconds
     * @return Pair of (mappedPhoneCaptureMs, cameraUncertaintyMs)
     */
    @Synchronized
    fun processFrameTimestamp(cameraUs: Long, phoneArrivalMs: Long): Pair<Long, Long> {
        val cameraMs = cameraUs / 1000L
        val instantOffset = phoneArrivalMs - cameraMs

        val syncedAt = explicitSyncAtMs
        if (syncedAt != null) {
            // Arrival includes transport delay; it must not replace a measured offset.
            val elapsed = (phoneArrivalMs - syncedAt).coerceAtLeast(0L)
            clockUncertaintyMs = explicitSyncUncertaintyMs + (elapsed + 4999L) / 5000L
        } else if (sampleCount == 0) {
            filteredOffsetMs = instantOffset
            minObservedDelayMs = instantOffset
            clockUncertaintyMs = 25L
            isClockKnown = true
        } else {
            // Keep lower envelope of delay (minimum latency sample is closest to true transport)
            if (instantOffset < filteredOffsetMs) {
                filteredOffsetMs = (filteredOffsetMs * 0.7 + instantOffset * 0.3).toLong()
            } else {
                filteredOffsetMs = (filteredOffsetMs * 0.95 + instantOffset * 0.05).toLong()
            }

            // Estimate jitter/uncertainty
            val jitter = abs(instantOffset - filteredOffsetMs)
            clockUncertaintyMs = max(10L, min(100L, (clockUncertaintyMs * 0.8 + jitter * 0.2).toLong() + 5L))
        }

        sampleCount++
        lastFrameCameraUs = cameraUs
        lastPhoneArrivalMs = phoneArrivalMs

        val mappedPhoneCaptureMs = cameraMs + filteredOffsetMs
        return Pair(mappedPhoneCaptureMs, clockUncertaintyMs)
    }

    /**
     * Updates offset explicitly from an active two-way sync exchange with camera node.
     */
    @Synchronized
    fun recordTwoWaySync(t0PhoneMs: Long, tCamUs: Long, t1PhoneMs: Long) {
        val rttMs = (t1PhoneMs - t0PhoneMs).coerceAtLeast(1L)
        val tCamMs = tCamUs / 1000L
        val phoneMidpoint = t0PhoneMs + (rttMs / 2)
        val measuredOffset = phoneMidpoint - tCamMs

        filteredOffsetMs = measuredOffset
        clockUncertaintyMs = ((rttMs + 1) / 2) + 2L
        explicitSyncAtMs = t1PhoneMs
        explicitSyncUncertaintyMs = clockUncertaintyMs
        isClockKnown = rttMs <= 300L
    }

    @Synchronized
    fun reset() {
        sampleCount = 0
        minObservedDelayMs = Long.MAX_VALUE
        filteredOffsetMs = 0L
        clockUncertaintyMs = 50L
        isClockKnown = false
        explicitSyncAtMs = null
        explicitSyncUncertaintyMs = 0L
    }
}

package com.rideshield.phone.protocol

import android.os.SystemClock

/**
 * Tracks controller-to-phone clock synchronization and validates observation freshness.
 * Accurately models the clock math from PHONE_PROTOCOL.md and phone.cpp.
 */
class RswClockSynchronizer(
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() }
) {
    @Volatile var isSynced: Boolean = false
        private set

    @Volatile var lastSyncPhoneMs: Long = 0L
        private set

    @Volatile var lastControllerSyncMs: Long = 0L
        private set

    @Volatile var estimatedOffsetMs: Long = 0L
        private set // controllerMs - phoneMs

    @Volatile var estimatedRttMs: Long = 0L
        private set

    @Volatile var uncertaintyMs: Long = 100L
        private set

    /**
     * Called when {"type":"sync","id":id,"controller_ms":controllerMs} arrives.
     * Records local phone arrival time and calculates provisional offset.
     */
    fun onSyncRequest(controllerMs: Long, syncId: Long): Long {
        val nowPhoneMs = clockProvider()
        lastSyncPhoneMs = nowPhoneMs
        lastControllerSyncMs = controllerMs

        // Provisional offset: controllerMs - phoneMs
        estimatedOffsetMs = controllerMs - nowPhoneMs
        isSynced = true
        return nowPhoneMs
    }

    /**
     * Updates RTT and uncertainty metrics when controller responds or acknowledges.
     */
    fun recordRttEstimate(rttMs: Long) {
        estimatedRttMs = rttMs
        // Uncertainty formula from PHONE_PROTOCOL.md: ceil(RTT/2) + 1 ms
        uncertaintyMs = ((rttMs + 1) / 2) + 1
    }

    /**
     * Checks if clock sync is still valid. Sync expires after 30 seconds (phone.cpp line 21).
     */
    fun isClockValid(): Boolean {
        if (!isSynced) return false
        val elapsed = clockProvider() - lastSyncPhoneMs
        return elapsed in 0..RswConstants.SYNC_EXPIRY_MS
    }

    /**
     * Estimates current controller monotonic clock.
     */
    fun estimateControllerMs(): Long {
        return clockProvider() + estimatedOffsetMs
    }

    /**
     * Calculates current clock uncertainty growing at 200 ppm (1 ms per 5000 ms) since last sync.
     * Matches phone.cpp line 38: error = uncertainty_ + p.auxiliary + (now - syncAt_ + 4999)/5000
     */
    fun getCurrentUncertainty(cameraUncertaintyMs: Long): Long {
        val now = clockProvider()
        val elapsedSinceSync = (now - lastSyncPhoneMs).coerceAtLeast(0L)
        val drift = (elapsedSinceSync + 4999L) / 5000L
        return uncertaintyMs + cameraUncertaintyMs + drift
    }

    /**
     * Validates whether an observation with given capture timestamp in phone domain
     * will be accepted by controller (< 500ms upper bound on frame age).
     * Returns true if fresh, false if stale.
     */
    fun isFrameFresh(capturePhoneMs: Long, cameraUncertaintyMs: Long): Boolean {
        val nowPhoneMs = clockProvider()
        val ageOnPhone = nowPhoneMs - capturePhoneMs
        if (ageOnPhone < 0) return false // from future

        val totalUncertainty = getCurrentUncertainty(cameraUncertaintyMs)
        // Upper bound check: age + error <= 500 ms (PHONE_PROTOCOL.md line 64)
        return (ageOnPhone + totalUncertainty) <= RswConstants.MAX_FRAME_AGE_MS
    }

    fun reset() {
        isSynced = false
        lastSyncPhoneMs = 0L
        lastControllerSyncMs = 0L
        estimatedOffsetMs = 0L
        estimatedRttMs = 0L
        uncertaintyMs = 100L
    }
}

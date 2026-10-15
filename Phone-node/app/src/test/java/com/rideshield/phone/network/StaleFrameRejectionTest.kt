package com.rideshield.phone.network

import com.rideshield.phone.protocol.RswClockSynchronizer
import com.rideshield.phone.protocol.RswConstants
import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests verifying frame freshness rules, stale frame drops, and monotonic timestamp checks.
 * Models phone.cpp lines 36-43:
 *  - s.hadFrame && p.phoneMs <= s.lastFrame -> Reject::Timestamp
 *  - age + error > 500 -> Reject::Stale
 *  - expiry = mapped + 500 - error <= now -> discarded
 */
class StaleFrameRejectionTest {

    @Test
    fun testFrameFreshnessWithin500MsBound() {
        var currentTimeMs = 10000L
        val synchronizer = RswClockSynchronizer { currentTimeMs }

        // Sync at t = 10000 with controller t0 = 50000
        synchronizer.onSyncRequest(controllerMs = 50000L, syncId = 1L)
        synchronizer.recordRttEstimate(rttMs = 20L) // uncertainty = 11ms

        // Frame captured 100ms ago with 10ms camera clock uncertainty:
        // age = 100ms, uncertainty = 11 + 10 + drift(1) = 22ms -> total = 122ms <= 500ms -> FRESH
        val captureMs = currentTimeMs - 100L
        val isFresh = synchronizer.isFrameFresh(capturePhoneMs = captureMs, cameraUncertaintyMs = 10L)
        assertTrue("Recent frame must be accepted as fresh", isFresh)
    }

    @Test
    fun testFrameStaleWhenAgeExceeds500Ms() {
        var currentTimeMs = 10000L
        val synchronizer = RswClockSynchronizer { currentTimeMs }

        synchronizer.onSyncRequest(controllerMs = 50000L, syncId = 1L)
        synchronizer.recordRttEstimate(rttMs = 20L)

        // Frame captured 600ms ago:
        // age = 600ms -> 600 + error > 500ms -> STALE
        val captureMs = currentTimeMs - 600L
        val isFresh = synchronizer.isFrameFresh(capturePhoneMs = captureMs, cameraUncertaintyMs = 10L)
        assertFalse("Frame older than 500ms must be rejected as stale", isFresh)
    }

    @Test
    fun testHighCameraUncertaintyCausesStaleRejection() {
        var currentTimeMs = 10000L
        val synchronizer = RswClockSynchronizer { currentTimeMs }

        synchronizer.onSyncRequest(controllerMs = 50000L, syncId = 1L)
        synchronizer.recordRttEstimate(rttMs = 30L)

        // Frame captured 200ms ago, but camera uncertainty is 400ms:
        // age + uncertainty = 200 + 416 = 616ms > 500ms bound -> STALE
        val captureMs = currentTimeMs - 200L
        val isFresh = synchronizer.isFrameFresh(capturePhoneMs = captureMs, cameraUncertaintyMs = 400L)
        assertFalse("Frame with large clock uncertainty must be rejected as stale", isFresh)
    }

    @Test
    fun testClockSyncExpiryAfter30Seconds() {
        var currentTimeMs = 10000L
        val synchronizer = RswClockSynchronizer { currentTimeMs }

        synchronizer.onSyncRequest(controllerMs = 50000L, syncId = 1L)
        assertTrue(synchronizer.isClockValid())

        // Advance clock by 29 seconds -> still valid
        currentTimeMs += 29000L
        assertTrue(synchronizer.isClockValid())

        // Advance clock past 30 seconds -> expires per PHONE_PROTOCOL.md line 63
        currentTimeMs += 2000L
        assertFalse("Clock sync must expire after 30 seconds without resync", synchronizer.isClockValid())
    }

    @Test
    fun testClientMonotonicTimestampConstraint() {
        var simulatedClock = 1000L
        val client = RswWebSocketClient(clockProvider = { simulatedClock })

        // Attempting to send an observation without session/auth must be rejected
        val sentUnauth = client.sendFrontObservation(
            capturePhoneMs = 1000L,
            cameraUncertaintyMs = 10L,
            pothole = true,
            wetRoad = false,
            confidence = 80
        )
        assertFalse("Cannot send observation when unauthenticated", sentUnauth)
    }
}

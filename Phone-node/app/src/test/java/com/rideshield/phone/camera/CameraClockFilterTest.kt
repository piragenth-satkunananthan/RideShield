package com.rideshield.phone.camera

import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests for CameraClockFilter mapping and uncertainty estimation.
 */
class CameraClockFilterTest {

    @Test
    fun testInitialTimestampProcessing() {
        val filter = CameraClockFilter()
        assertFalse(filter.isClockKnown)

        // Camera hardware timer at 1,000,000 us (1,000 ms)
        // Arrives at phone at 5,000 ms
        val (mappedCaptureMs, uncertaintyMs) = filter.processFrameTimestamp(
            cameraUs = 1000000L,
            phoneArrivalMs = 5000L
        )

        assertTrue(filter.isClockKnown)
        assertEquals(5000L, mappedCaptureMs)
        assertTrue(uncertaintyMs in 10L..100L)
    }

    @Test
    fun testTwoWaySyncCalculatesMidpointAndUncertainty() {
        val filter = CameraClockFilter()

        // Phone sends ping at t0 = 1000ms
        // Camera responds with tCam = 2,500,000 us (2500ms)
        // Response arrives at phone at t1 = 1040ms (RTT = 40ms)
        // Midpoint on phone = 1020ms
        // Offset = 1020 - 2500 = -1480ms
        // Uncertainty = (40 + 1)/2 + 2 = 22ms
        filter.recordTwoWaySync(
            t0PhoneMs = 1000L,
            tCamUs = 2500000L,
            t1PhoneMs = 1040L
        )

        assertTrue(filter.isClockKnown)
        assertEquals(-1480L, filter.filteredOffsetMs)
        assertEquals(22L, filter.clockUncertaintyMs)

        // When a frame arrives from camera at tCam = 3,000,000 us (3000ms)
        val (mappedMs, _) = filter.processFrameTimestamp(
            cameraUs = 3000000L,
            phoneArrivalMs = 1530L
        )

        // Mapped capture should be 3000 + (-1480) = 1520ms
        assertEquals(1520L, mappedMs)
    }
}

package com.rideshield.phone.camera

import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests verifying BoundedFrameQueue behavior and newest-frame-drop-oldest semantics.
 */
class BoundedFrameQueueTest {

    private fun createDummyFrame(index: Long, arrivalMs: Long): CameraFrame {
        return CameraFrame(
            cameraNode = "FRONT",
            frameIndex = index,
            cameraCaptureTimestampUs = index * 33333L,
            jpegBytes = byteArrayOf(1, 2, 3),
            phoneArrivalMs = arrivalMs,
            mappedPhoneCaptureMs = arrivalMs,
            clockUncertaintyMs = 10L
        )
    }

    @Test
    fun testCapacityOneReplacesOlderFrames() {
        val queue = BoundedFrameQueue(capacity = 1)
        assertEquals(0, queue.size)
        assertEquals(0L, queue.totalDropped)

        // Push frame 1
        queue.push(createDummyFrame(1L, 1000L))
        assertEquals(1, queue.size)
        assertEquals(0L, queue.totalDropped)

        // Push frame 2 -> frame 1 must be dropped
        queue.push(createDummyFrame(2L, 1033L))
        assertEquals(1, queue.size)
        assertEquals(1L, queue.totalDropped)

        // Push frame 3 -> frame 2 must be dropped
        queue.push(createDummyFrame(3L, 1066L))
        assertEquals(1, queue.size)
        assertEquals(2L, queue.totalDropped)

        // Poll must return frame 3 (newest frame)
        val newest = queue.poll()
        assertNotNull(newest)
        assertEquals(3L, newest?.frameIndex)
        assertEquals(0, queue.size)
    }

    @Test
    fun testBurstPushDropCounter() {
        val queue = BoundedFrameQueue(capacity = 1)

        // Push 10 frames
        for (i in 1..10) {
            queue.push(createDummyFrame(i.toLong(), 1000L + i * 33L))
        }

        assertEquals(10L, queue.totalPushed)
        assertEquals(9L, queue.totalDropped)
        assertEquals(1, queue.size)

        val item = queue.poll()
        assertEquals(10L, item?.frameIndex)
    }
}

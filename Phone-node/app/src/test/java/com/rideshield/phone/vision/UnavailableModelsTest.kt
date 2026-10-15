package com.rideshield.phone.vision

import android.graphics.Bitmap
import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests verifying that unavailable models strictly report UNAVAILABLE
 * and NEVER generate invented or synthetic detections.
 */
class UnavailableModelsTest {

    @Test
    fun testWetRoadClassifierUnavailableBehavior() {
        val classifier = UnavailableWetRoadClassifier()
        assertFalse("Initialization must return false when weights unavailable", classifier.initialize())

        val info = classifier.getModelInfo()
        assertEquals(ModelStatus.UNAVAILABLE, info.status)
        assertEquals("UNAVAILABLE", info.version)

        // Pass a mock 10x10 bitmap
        val dummyBitmap = Bitmap.createBitmap(10, 10, Bitmap.Config.ARGB_8888)
        val result = classifier.classify(dummyBitmap)

        assertEquals("Status must be UNAVAILABLE", ModelStatus.UNAVAILABLE, result.status)
        assertFalse("Must never invent a wet road hazard", result.isWetRoad)
        assertEquals("Confidence must be 0 for unavailable model", 0, result.confidence)
    }

    @Test
    fun testRearVehicleDetectorUnavailableBehavior() {
        val detector = UnavailableRearVehicleDetector()
        assertFalse("Initialization must return false when weights unavailable", detector.initialize())

        val info = detector.getModelInfo()
        assertEquals(ModelStatus.UNAVAILABLE, info.status)
        assertEquals("UNAVAILABLE", info.version)

        val dummyBitmap = Bitmap.createBitmap(10, 10, Bitmap.Config.ARGB_8888)
        val result = detector.detectAndTrack(dummyBitmap)

        assertEquals("Status must be UNAVAILABLE", ModelStatus.UNAVAILABLE, result.status)
        assertTrue("Detected vehicles list must be strictly empty", result.detectedVehicles.isEmpty())
        assertFalse("Must never invent a rear vehicle hazard", result.isHazardPresent)
        assertEquals("Confidence must be 0 for unavailable model", 0, result.highestConfidence)
    }

    @Test
    fun testNoInventedDetectionsAllowedInRelease() {
        val wetClassifier = UnavailableWetRoadClassifier()
        val rearDetector = UnavailableRearVehicleDetector()

        val bitmap = Bitmap.createBitmap(10, 10, Bitmap.Config.ARGB_8888)

        // Multiple consecutive calls to ensure no sporadic or random spoofed detections occur
        for (i in 0 until 50) {
            val wet = wetClassifier.classify(bitmap)
            assertFalse(wet.isWetRoad)
            assertEquals(0, wet.confidence)

            val rear = rearDetector.detectAndTrack(bitmap)
            assertFalse(rear.isHazardPresent)
            assertTrue(rear.detectedVehicles.isEmpty())
        }
    }
}

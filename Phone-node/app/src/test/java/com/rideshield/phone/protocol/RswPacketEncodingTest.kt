package com.rideshield.phone.protocol

import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests verifying exact RSW1 64-byte Little-Endian wire encoding.
 * Verifies byte-for-byte fidelity against:
 *  - dev_version/rideshield-core/lib/RideShieldCore/src/phone.cpp
 *  - dev_version/rideshield-core/tests/test_tools.py (line 8)
 *  - dev_version/rideshield-core/tests/test_core.cpp (line 69)
 */
class RswPacketEncodingTest {

    @Test
    fun testWireGoldenVector() {
        // Matches test_tools.py: packet(4, 42, 3, 2010, source=1, flags=11, hazards=1, confidence=90)
        val packet = RswPacket(
            type = RswConstants.TYPE_OBSERVATION,
            source = RswConstants.SOURCE_FRONT,
            flags = 11.toByte(), // 0x0B = usable | model-valid | capture-clock-known
            hazards = RswConstants.HAZARD_POTHOLE,
            session = 42L,
            sequence = 3L,
            phoneMs = 2010L,
            auxiliary = 0L,
            zone = RswConstants.ZONE_UNKNOWN,
            confidence = 90.toByte()
        )

        val bytes = RswCodec.encode(packet)
        assertEquals(64, bytes.size)

        val hex = RswCodec.toHexString(bytes)
        val expectedHex = "5253573104010b012a00000003000000da0700000000000000000000005a0000" + "00".repeat(32)

        assertEquals("Golden vector hex mismatch", expectedHex, hex)

        // Test roundtrip decode
        val decoded = RswCodec.decode(bytes)
        assertNotNull("Failed to decode golden vector", decoded)
        assertEquals(packet, decoded)
    }

    @Test
    fun testAuthPacketTokenRequirements() {
        val validToken = "bench-token-change-before-use-01"
        assertEquals(32, validToken.length)

        val auth = RswPacket.createAuth(
            session = 100L,
            sequence = 1L,
            tokenString = validToken
        )
        val encoded = RswCodec.encode(auth)
        assertEquals(64, encoded.size)

        val decoded = RswCodec.decode(encoded)
        assertNotNull(decoded)
        assertEquals(1.toByte(), decoded?.type)
        assertEquals(1L, decoded?.sequence)
        assertEquals(100L, decoded?.session)
        assertEquals(validToken, String(decoded!!.token, Charsets.US_ASCII))

        // Invalid token length must fail factory validation
        try {
            RswPacket.createAuth(100L, 1L, "too-short")
            fail("Expected exception for short token")
        } catch (e: IllegalArgumentException) {
            // Expected
        }
    }

    @Test
    fun testNonAuthPacketsMustHaveZeroedToken() {
        val hb = RswPacket.createHeartbeat(session = 5L, sequence = 2L, phoneMonotonicMs = 1500L)
        val encoded = RswCodec.encode(hb)

        // Verify token bytes are all 0
        for (i in 32 until 64) {
            assertEquals("Byte at offset $i must be zero", 0.toByte(), encoded[i])
        }

        // Corrupt token byte in non-AUTH packet -> must fail decoding
        encoded[32] = 1.toByte()
        assertNull("Decoder must reject non-zero token on heartbeat", RswCodec.decode(encoded))
    }

    @Test
    fun testReservedBytesMustBeZero() {
        val packet = RswPacket.createHeartbeat(session = 1L, sequence = 2L, phoneMonotonicMs = 1000L)
        val encoded = RswCodec.encode(packet)

        // Bytes 30..31 are reserved
        assertEquals(0.toByte(), encoded[30])
        assertEquals(0.toByte(), encoded[31])

        encoded[30] = 1.toByte()
        assertNull("Decoder must reject non-zero reserved byte 30", RswCodec.decode(encoded))

        encoded[30] = 0.toByte()
        encoded[31] = 1.toByte()
        assertNull("Decoder must reject non-zero reserved byte 31", RswCodec.decode(encoded))
    }

    @Test
    fun testFrontObservationSchemaConstraints() {
        // Front observation cannot have rear-vehicle hazard (bit 4)
        try {
            val bad = RswPacket(
                type = RswConstants.TYPE_OBSERVATION,
                source = RswConstants.SOURCE_FRONT,
                hazards = RswConstants.HAZARD_REAR_VEHICLE,
                session = 1L,
                sequence = 2L,
                phoneMs = 1000L
            )
            RswCodec.encode(bad)
            fail("Front observation with rear-vehicle hazard must fail encode")
        } catch (e: IllegalArgumentException) {
            // Expected
        }

        // Front observation cannot have non-UNKNOWN zone
        try {
            val bad = RswPacket(
                type = RswConstants.TYPE_OBSERVATION,
                source = RswConstants.SOURCE_FRONT,
                zone = RswConstants.ZONE_LEFT,
                session = 1L,
                sequence = 2L,
                phoneMs = 1000L
            )
            RswCodec.encode(bad)
            fail("Front observation with non-UNKNOWN zone must fail encode")
        } catch (e: IllegalArgumentException) {
            // Expected
        }
    }

    @Test
    fun testRearObservationSchemaConstraints() {
        // Rear observation cannot have pothole or wet road hazards
        try {
            val bad = RswPacket(
                type = RswConstants.TYPE_OBSERVATION,
                source = RswConstants.SOURCE_REAR,
                hazards = RswConstants.HAZARD_POTHOLE,
                session = 1L,
                sequence = 2L,
                phoneMs = 1000L
            )
            RswCodec.encode(bad)
            fail("Rear observation with pothole hazard must fail encode")
        } catch (e: IllegalArgumentException) {
            // Expected
        }
    }

    @Test
    fun testAuxiliaryLimits() {
        // AUTH and HEARTBEAT auxiliary must be 0
        try {
            val bad = RswPacket(
                type = RswConstants.TYPE_HEARTBEAT,
                session = 1L,
                sequence = 2L,
                auxiliary = 100L
            )
            RswCodec.encode(bad)
            fail("Heartbeat with auxiliary != 0 must fail encode")
        } catch (e: IllegalArgumentException) {
            // Expected
        }

        // Observation auxiliary > 10000ms must fail
        try {
            val bad = RswPacket(
                type = RswConstants.TYPE_OBSERVATION,
                source = RswConstants.SOURCE_FRONT,
                session = 1L,
                sequence = 2L,
                auxiliary = 10001L
            )
            RswCodec.encode(bad)
            fail("Observation auxiliary > 10000 must fail encode")
        } catch (e: IllegalArgumentException) {
            // Expected
        }
    }
}

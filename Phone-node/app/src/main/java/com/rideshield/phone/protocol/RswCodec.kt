package com.rideshield.phone.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Encodes and decodes the exact 64-byte Little-Endian RSW1 wire packets.
 * Strictly enforces all constraints from:
 *  - dev_version/rideshield-core/lib/RideShieldCore/src/phone.cpp (decodePacket / encodePacket)
 *  - tools/phone_sim.py (struct '<4sBBBBIIQIBBH32s')
 */
object RswCodec {

    /**
     * Encodes a packet to 64 bytes in Little-Endian byte order.
     */
    fun encode(packet: RswPacket): ByteArray {
        validateOutgoingPacket(packet)

        val buffer = ByteBuffer.allocate(RswConstants.PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN)

        // 0..3: Magic ASCII "RSW1"
        buffer.put(RswConstants.MAGIC)

        // 4: Type (uint8)
        buffer.put(packet.type)

        // 5: Source (uint8)
        buffer.put(packet.source)

        // 6: Flags (uint8)
        buffer.put(packet.flags)

        // 7: Hazards (uint8)
        buffer.put(packet.hazards)

        // 8..11: Session (uint32, little-endian)
        buffer.putInt((packet.session and 0xFFFFFFFFL).toInt())

        // 12..15: Sequence (uint32, little-endian)
        buffer.putInt((packet.sequence and 0xFFFFFFFFL).toInt())

        // 16..23: Phone monotonic milliseconds (uint64, little-endian)
        buffer.putLong(packet.phoneMs)

        // 24..27: Auxiliary (uint32, little-endian)
        buffer.putInt((packet.auxiliary and 0xFFFFFFFFL).toInt())

        // 28: Zone (uint8)
        buffer.put(packet.zone)

        // 29: Confidence (uint8)
        buffer.put(packet.confidence)

        // 30..31: Reserved uint16 (zeros)
        buffer.putShort(0.toShort())

        // 32..63: Token (32 bytes)
        buffer.put(packet.token)

        return buffer.array()
    }

    /**
     * Decodes a 64-byte buffer into an RswPacket.
     * Returns null if any validation check fails, matching phone.cpp decodePacket().
     */
    fun decode(bytes: ByteArray): RswPacket? {
        if (bytes.size != RswConstants.PACKET_SIZE) return null

        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)

        // Offset 0..3: Magic check
        val magic = ByteArray(4)
        buffer.get(magic)
        if (!magic.contentEquals(RswConstants.MAGIC)) return null

        // Offset 4: Type
        val type = buffer.get()
        if (type < 1 || type > 4) return null

        // Offset 5: Source
        val source = buffer.get()
        if (source < 0 || source > 2) return null

        // Offset 6: Flags (upper 4 bits must be 0)
        val flags = buffer.get()
        if ((flags.toInt() and 0xF0) != 0) return null

        // Offset 7: Hazards (upper 5 bits must be 0)
        val hazards = buffer.get()
        if ((hazards.toInt() and 0xF8) != 0) return null

        // Offset 8..11: Session (uint32)
        val session = buffer.getInt().toLong() and 0xFFFFFFFFL

        // Offset 12..15: Sequence (uint32)
        val sequence = buffer.getInt().toLong() and 0xFFFFFFFFL

        // Offset 16..23: PhoneMs (uint64)
        val phoneMs = buffer.getLong()

        // Offset 24..27: Auxiliary (uint32)
        val auxiliary = buffer.getInt().toLong() and 0xFFFFFFFFL

        // Offset 28: Zone
        val zone = buffer.get()
        if (zone < 0 || zone > 3) return null

        // Offset 29: Confidence
        val confidence = buffer.get()
        val confInt = confidence.toInt() and 0xFF
        if (confInt > 100) return null

        // Offset 30..31: Reserved (must be 0)
        val res0 = buffer.get()
        val res1 = buffer.get()
        if (res0 != 0.toByte() || res1 != 0.toByte()) return null

        // Offset 32..63: Token (32 bytes)
        val token = ByteArray(RswConstants.TOKEN_LENGTH)
        buffer.get(token)

        // Global invariant checks matching phone.cpp line 9:
        if (session == 0L || sequence == 0L || phoneMs < 0 || phoneMs > RswConstants.MAX_PHONE_MS) {
            return null
        }

        // Observation packet specific checks (phone.cpp line 10)
        if (type == RswConstants.TYPE_OBSERVATION) {
            if (source == RswConstants.SOURCE_NONE) return null
            if (auxiliary > RswConstants.MAX_OBSERVATION_UNCERTAINTY_MS) return null
            if (source == RswConstants.SOURCE_FRONT) {
                // Front allows pothole/wet only, zone unknown
                if ((hazards.toInt() and 4) != 0 || zone != RswConstants.ZONE_UNKNOWN) return null
            }
            if (source == RswConstants.SOURCE_REAR) {
                // Rear allows rear-vehicle only
                if ((hazards.toInt() and 3) != 0) return null
            }
        } else {
            // Control packets: source, flags, hazards, zone, confidence must be 0 (phone.cpp line 11)
            if (source != RswConstants.SOURCE_NONE || flags != 0.toByte() || hazards != 0.toByte() ||
                zone != RswConstants.ZONE_UNKNOWN || confidence != 0.toByte()
            ) {
                return null
            }
        }

        // Non-AUTH packets: token must be all zeros (phone.cpp line 12)
        if (type != RswConstants.TYPE_AUTH) {
            for (b in token) {
                if (b != 0.toByte()) return null
            }
        }

        // AUTH and HEARTBEAT packets: auxiliary must be zero (phone.cpp line 13)
        if ((type == RswConstants.TYPE_AUTH || type == RswConstants.TYPE_HEARTBEAT) && auxiliary != 0L) {
            return null
        }

        return RswPacket(
            type = type,
            source = source,
            flags = flags,
            hazards = hazards,
            session = session,
            sequence = sequence,
            phoneMs = phoneMs,
            auxiliary = auxiliary,
            zone = zone,
            confidence = confidence,
            token = token
        )
    }

    private fun validateOutgoingPacket(p: RswPacket) {
        require(p.type in 1..4) { "Invalid packet type: ${p.type}" }
        require(p.source in 0..2) { "Invalid packet source: ${p.source}" }
        require(p.session != 0L) { "Session must be non-zero" }
        require(p.sequence != 0L) { "Sequence must be non-zero" }
        require(p.phoneMs in 0..RswConstants.MAX_PHONE_MS) { "phoneMs out of range: ${p.phoneMs}" }

        if (p.type == RswConstants.TYPE_OBSERVATION) {
            require(p.source != RswConstants.SOURCE_NONE) { "Observation must have a valid source" }
            require(p.auxiliary <= RswConstants.MAX_OBSERVATION_UNCERTAINTY_MS) {
                "Camera clock uncertainty must be <= 10000ms"
            }
            if (p.source == RswConstants.SOURCE_FRONT) {
                require((p.hazards.toInt() and 4) == 0) { "Front observation cannot contain rear-vehicle hazard" }
                require(p.zone == RswConstants.ZONE_UNKNOWN) { "Front observation must have zone=UNKNOWN" }
            }
            if (p.source == RswConstants.SOURCE_REAR) {
                require((p.hazards.toInt() and 3) == 0) { "Rear observation cannot contain pothole or wet road hazards" }
            }
        } else {
            require(p.source == RswConstants.SOURCE_NONE && p.flags == 0.toByte() &&
                    p.hazards == 0.toByte() && p.zone == RswConstants.ZONE_UNKNOWN &&
                    p.confidence == 0.toByte()) {
                "Control packets must have zero source, flags, hazards, zone, and confidence"
            }
            if (p.type != RswConstants.TYPE_AUTH) {
                require(p.token.all { it == 0.toByte() }) { "Non-AUTH packet must have zeroed token" }
            }
            if (p.type == RswConstants.TYPE_AUTH || p.type == RswConstants.TYPE_HEARTBEAT) {
                require(p.auxiliary == 0L) { "AUTH and HEARTBEAT auxiliary must be 0" }
            }
        }
    }

    fun toHexString(bytes: ByteArray): String =
        bytes.joinToString("") { "%02x".format(it) }

    fun fromHexString(hex: String): ByteArray {
        val clean = hex.trim().replace(" ", "")
        val result = ByteArray(clean.length / 2)
        for (i in result.indices) {
            result[i] = clean.substring(i * 2, i * 2 + 2).toInt(16).toByte()
        }
        return result
    }
}

package com.rideshield.phone.protocol

import java.util.Arrays

/**
 * RideShield WebSocket v1 (RSW1) 64-byte binary packet model.
 */
data class RswPacket(
    val type: Byte,
    val source: Byte = RswConstants.SOURCE_NONE,
    val flags: Byte = 0,
    val hazards: Byte = 0,
    val session: Long = 0,          // uint32 (stored as Long to support 32-bit unsigned)
    val sequence: Long = 0,         // uint32 (stored as Long, strictly increasing)
    val phoneMs: Long = 0,          // uint64 (stored as Long, max 2^52)
    val auxiliary: Long = 0,        // uint32 (sync request ID or observation uncertainty in ms)
    val zone: Byte = RswConstants.ZONE_UNKNOWN,
    val confidence: Byte = 0,       // 0..100
    val token: ByteArray = ByteArray(RswConstants.TOKEN_LENGTH) // 32 bytes
) {
    init {
        require(token.size == RswConstants.TOKEN_LENGTH) {
            "Token array must be exactly ${RswConstants.TOKEN_LENGTH} bytes"
        }
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false

        other as RswPacket
        if (type != other.type) return false
        if (source != other.source) return false
        if (flags != other.flags) return false
        if (hazards != other.hazards) return false
        if (session != other.session) return false
        if (sequence != other.sequence) return false
        if (phoneMs != other.phoneMs) return false
        if (auxiliary != other.auxiliary) return false
        if (zone != other.zone) return false
        if (confidence != other.confidence) return false
        if (!Arrays.equals(token, other.token)) return false

        return true
    }

    override fun hashCode(): Int {
        var result = type.toInt()
        result = 31 * result + source.toInt()
        result = 31 * result + flags.toInt()
        result = 31 * result + hazards.toInt()
        result = 31 * result + session.hashCode()
        result = 31 * result + sequence.hashCode()
        result = 31 * result + phoneMs.hashCode()
        result = 31 * result + auxiliary.hashCode()
        result = 31 * result + zone.toInt()
        result = 31 * result + confidence.toInt()
        result = 31 * result + Arrays.hashCode(token)
        return result
    }

    companion object {
        /**
         * Factory: Binary AUTH packet (Sequence must be 1 per protocol specification).
         */
        fun createAuth(session: Long, sequence: Long, tokenString: String): RswPacket {
            require(tokenString.length == 32 && tokenString.all { it.code in 32..126 }) { "Token must be 32 ASCII characters" }
            val tokenBytes = ByteArray(RswConstants.TOKEN_LENGTH)
            val asciiBytes = tokenString.toByteArray(Charsets.US_ASCII)
            require(asciiBytes.size == RswConstants.TOKEN_LENGTH) {
                "AUTH token must be exactly ${RswConstants.TOKEN_LENGTH} ASCII bytes, got ${asciiBytes.size}"
            }
            System.arraycopy(asciiBytes, 0, tokenBytes, 0, RswConstants.TOKEN_LENGTH)

            return RswPacket(
                type = RswConstants.TYPE_AUTH,
                source = RswConstants.SOURCE_NONE,
                flags = 0,
                hazards = 0,
                session = session,
                sequence = sequence,
                phoneMs = 0,
                auxiliary = 0,
                zone = RswConstants.ZONE_UNKNOWN,
                confidence = 0,
                token = tokenBytes
            )
        }

        /**
         * Factory: Binary SYNC response packet.
         */
        fun createSync(session: Long, sequence: Long, syncId: Long, phoneMonotonicMs: Long): RswPacket {
            return RswPacket(
                type = RswConstants.TYPE_SYNC,
                source = RswConstants.SOURCE_NONE,
                flags = 0,
                hazards = 0,
                session = session,
                sequence = sequence,
                phoneMs = phoneMonotonicMs,
                auxiliary = syncId,
                zone = RswConstants.ZONE_UNKNOWN,
                confidence = 0
            )
        }

        /**
         * Factory: Binary HEARTBEAT packet.
         */
        fun createHeartbeat(session: Long, sequence: Long, phoneMonotonicMs: Long): RswPacket {
            return RswPacket(
                type = RswConstants.TYPE_HEARTBEAT,
                source = RswConstants.SOURCE_NONE,
                flags = 0,
                hazards = 0,
                session = session,
                sequence = sequence,
                phoneMs = phoneMonotonicMs,
                auxiliary = 0,
                zone = RswConstants.ZONE_UNKNOWN,
                confidence = 0
            )
        }

        /**
         * Factory: Binary OBSERVATION packet for Front Camera.
         * Enforces Front constraints: no rear vehicle hazard, zone must be UNKNOWN.
         */
        fun createFrontObservation(
            session: Long,
            sequence: Long,
            capturePhoneMs: Long,
            cameraUncertaintyMs: Long,
            pothole: Boolean,
            wetRoad: Boolean,
            confidence: Int,
            usable: Boolean = true,
            modelValid: Boolean = true,
            captureClockKnown: Boolean = true
        ): RswPacket {
            var flags: Byte = 0
            if (usable) flags = (flags.toInt() or RswConstants.FLAG_USABLE.toInt()).toByte()
            if (modelValid) flags = (flags.toInt() or RswConstants.FLAG_MODEL_VALID.toInt()).toByte()
            if (captureClockKnown) flags = (flags.toInt() or RswConstants.FLAG_CAPTURE_CLOCK_KNOWN.toInt()).toByte()

            var hazards: Byte = 0
            if (pothole) hazards = (hazards.toInt() or RswConstants.HAZARD_POTHOLE.toInt()).toByte()
            if (wetRoad) hazards = (hazards.toInt() or RswConstants.HAZARD_WET.toInt()).toByte()

            return RswPacket(
                type = RswConstants.TYPE_OBSERVATION,
                source = RswConstants.SOURCE_FRONT,
                flags = flags,
                hazards = hazards,
                session = session,
                sequence = sequence,
                phoneMs = capturePhoneMs,
                auxiliary = cameraUncertaintyMs.coerceIn(0L, RswConstants.MAX_OBSERVATION_UNCERTAINTY_MS),
                zone = RswConstants.ZONE_UNKNOWN,
                confidence = confidence.coerceIn(0, 100).toByte()
            )
        }

        /**
         * Factory: Binary OBSERVATION packet for Rear Camera.
         * Enforces Rear constraints: no pothole/wet hazards, rear vehicle only.
         */
        fun createRearObservation(
            session: Long,
            sequence: Long,
            capturePhoneMs: Long,
            cameraUncertaintyMs: Long,
            rearVehicle: Boolean,
            confidence: Int,
            zone: Byte = RswConstants.ZONE_UNKNOWN,
            usable: Boolean = true,
            modelValid: Boolean = true,
            geometryValid: Boolean = false,
            captureClockKnown: Boolean = true
        ): RswPacket {
            var flags: Byte = 0
            if (usable) flags = (flags.toInt() or RswConstants.FLAG_USABLE.toInt()).toByte()
            if (modelValid) flags = (flags.toInt() or RswConstants.FLAG_MODEL_VALID.toInt()).toByte()
            if (geometryValid) flags = (flags.toInt() or RswConstants.FLAG_GEOMETRY_VALID.toInt()).toByte()
            if (captureClockKnown) flags = (flags.toInt() or RswConstants.FLAG_CAPTURE_CLOCK_KNOWN.toInt()).toByte()

            val hazards: Byte = if (rearVehicle) RswConstants.HAZARD_REAR_VEHICLE else RswConstants.HAZARD_NONE

            return RswPacket(
                type = RswConstants.TYPE_OBSERVATION,
                source = RswConstants.SOURCE_REAR,
                flags = flags,
                hazards = hazards,
                session = session,
                sequence = sequence,
                phoneMs = capturePhoneMs,
                auxiliary = cameraUncertaintyMs.coerceIn(0L, RswConstants.MAX_OBSERVATION_UNCERTAINTY_MS),
                zone = zone.coerceIn(0, 3).toByte(),
                confidence = confidence.coerceIn(0, 100).toByte()
            )
        }
    }
}

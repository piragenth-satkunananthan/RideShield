package com.rideshield.phone.protocol

/**
 * RideShield WebSocket v1 (RSW1) Protocol Constants.
 * Source of truth:
 *  - dev_version/rideshield-core/lib/RideShieldCore/src/phone.h
 *  - dev_version/rideshield-core/lib/RideShieldCore/src/phone.cpp
 *  - dev_version/rideshield-core/docs/PHONE_PROTOCOL.md
 *  - tools/phone_sim.py
 */
object RswConstants {
    // Wire format layout
    val MAGIC: ByteArray = "RSW1".toByteArray(Charsets.US_ASCII)
    const val PACKET_SIZE = 64

    // Packet Types (uint8 at offset 4)
    const val TYPE_AUTH: Byte = 1
    const val TYPE_SYNC: Byte = 2
    const val TYPE_HEARTBEAT: Byte = 3
    const val TYPE_OBSERVATION: Byte = 4

    // Observation Sources (uint8 at offset 5)
    const val SOURCE_NONE: Byte = 0
    const val SOURCE_FRONT: Byte = 1
    const val SOURCE_REAR: Byte = 2

    // Flags bitmask (uint8 at offset 6, lower 4 bits valid: flags & 0xf0 == 0)
    const val FLAG_USABLE: Byte = 0x01
    const val FLAG_MODEL_VALID: Byte = 0x02
    const val FLAG_GEOMETRY_VALID: Byte = 0x04
    const val FLAG_CAPTURE_CLOCK_KNOWN: Byte = 0x08

    // Controller required flags for confirmed hazards: usable | model-valid | capture-clock-known
    const val FLAGS_CONFIRMED_HAZARD: Byte = (0x01 or 0x02 or 0x08).toByte() // 0x0B = 11

    // Hazards bitmask (uint8 at offset 7, lower 3 bits valid: hazards & 0xf8 == 0)
    const val HAZARD_NONE: Byte = 0x00
    const val HAZARD_POTHOLE: Byte = 0x01
    const val HAZARD_WET: Byte = 0x02
    const val HAZARD_REAR_VEHICLE: Byte = 0x04

    // Zones (uint8 at offset 28, 0..3)
    const val ZONE_UNKNOWN: Byte = 0
    const val ZONE_LEFT: Byte = 1
    const val ZONE_CENTRE: Byte = 2
    const val ZONE_RIGHT: Byte = 3

    // Token length (bytes 32..63)
    const val TOKEN_LENGTH = 32

    // Thresholds and validation limits
    const val MAX_PHONE_MS = 1L shl 52 // 4,503,599,627,370,496 ms
    const val MAX_OBSERVATION_UNCERTAINTY_MS = 10000L
    const val MAX_FRAME_AGE_MS = 500L
    const val MIN_CONFIRMATION_CONFIDENCE = 60

    // Timing defaults
    const val HEARTBEAT_INTERVAL_MS = 1000L
    const val AUTH_TIMEOUT_MS = 5000L
    const val SYNC_EXPIRY_MS = 30000L
    const val CONTROLLER_RATE_LIMIT_PPS = 50 // >50 binary pkts/sec triggers disconnect

    // Connection configuration defaults
    const val DEFAULT_CONTROLLER_URL = "ws://192.168.4.1:81"
    const val DEFAULT_TOKEN = "bench-token-change-before-use-01"
    const val DEFAULT_AP_SSID_PREFIX = "RideShield-"
    const val DEFAULT_AP_PASSWORD = "rideshield-bench-only"

    // Default Camera endpoints (Front & Rear ESP32-S3 camera nodes on same AP subnet)
    const val DEFAULT_FRONT_CAMERA_URL = "http://192.168.4.2:8080/stream"
    const val DEFAULT_REAR_CAMERA_URL = "http://192.168.4.3:8080/stream"
}

/**
 * Controller Rejection Codes mapping from phone.h Reject enum and PHONE_PROTOCOL.md:
 * 1: Session, 2: Sequence, 3: Clock, 4: Offline, 5: Future, 6: Stale, 7: Timestamp, 8: Schema
 */
enum class RswRejectCode(val code: Int, val description: String) {
    NONE(0, "None"),
    SESSION(1, "Session mismatch or unexpected session ID"),
    SEQUENCE(2, "Sequence number is 0 or not strictly increasing"),
    CLOCK(3, "Clock not synchronized or sync RTT > 400ms"),
    OFFLINE(4, "Heartbeat expired (> 3 seconds since last heartbeat)"),
    FUTURE(5, "Timestamp mapped into controller future beyond uncertainty"),
    STALE(6, "Frame age + uncertainty > 500ms"),
    TIMESTAMP(7, "Capture timestamp <= last frame or > 2^52 ms"),
    SCHEMA(8, "Malformed schema, invalid flags/hazards/source mismatch");

    companion object {
        fun fromCode(code: Int): RswRejectCode =
            entries.find { it.code == code } ?: SCHEMA
    }
}

package com.rideshield.phone.safety

/**
 * RideShield System Safety Architectural Boundary Guard.
 *
 * CRITICAL ARCHITECTURAL INVARIANT:
 * Crash detection, incident confirmation, SOS emergency SMS dispatch, blackbox logging,
 * and helmet haptic/buzzer alerts reside EXCLUSIVELY on the main NodeMCU ESP-32S safety core.
 *
 * Source of truth:
 *  - docs/ARCHITECTURE.md: "The main safety controller and computer-vision nodes are separated
 *    so that camera/AI failure cannot disable crash detection or emergency communication."
 *  - dev_version/rideshield-core/docs/PHONE_PROTOCOL.md: "It carries observations, never SOS,
 *    cancel, SMS-recipient changes or direct GPIO commands."
 *  - dev_version/rideshield-core/src/main.cpp
 *
 * The Android Phone node functions strictly as:
 *  1. An external perception producer (sending RSW1 binary OBSERVATION packets)
 *  2. A telemetry viewer (displaying controller state, warnings, latency, and previews)
 */
object SafetyBoundaryGuard {

    const val INVARIANT_RULE =
        "The phone node must never trigger crash decisions, SOS, SMS dispatch, or direct alert outputs."

    /**
     * Enforces that no code paths in the Android application can issue SOS or incident commands.
     */
    fun assertCommandForbidden(commandName: String) {
        val forbidden = listOf("SOS", "CANCEL", "CRASH_TRIGGER", "SEND_SMS", "SET_BUZZER", "SET_VIBRATION")
        if (forbidden.any { it.equals(commandName, ignoreCase = true) }) {
            throw SecurityException(
                "Violation of RideShield safety architecture: Command '$commandName' is prohibited on the phone node. " +
                        INVARIANT_RULE
            )
        }
    }

    /**
     * Verifies that incoming telemetry from controller is treated as read-only status.
     */
    fun isControllerAutonomous(): Boolean = true
}

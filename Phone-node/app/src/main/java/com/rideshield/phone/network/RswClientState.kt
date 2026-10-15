package com.rideshield.phone.network

import com.rideshield.phone.protocol.RswRejectCode

enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    AUTHENTICATING,
    SYNCING,
    CONNECTED_ACTIVE,
    RECONNECTING,
    ERROR
}

data class RswClientTelemetry(
    val status: ConnectionStatus = ConnectionStatus.DISCONNECTED,
    val serverUrl: String = "",
    val sessionId: Long = 0L,
    val currentSequence: Long = 0L,
    val isClockSynced: Boolean = false,
    val clockOffsetMs: Long = 0L,
    val rttMs: Long = 0L,
    val clockUncertaintyMs: Long = 0L,
    val totalSentPackets: Long = 0L,
    val sentObservations: Long = 0L,
    val sentHeartbeats: Long = 0L,
    val rejectedPackets: Long = 0L,
    val lastRejectCode: RswRejectCode = RswRejectCode.NONE,
    val controllerProfile: String = "DISCONNECTED",
    val controllerState: String = "OFFLINE",
    val controllerSendState: String = "NONE",
    val controllerWarning: String = "NONE",
    val controllerFrontValid: Boolean = false,
    val controllerRearValid: Boolean = false,
    val statusMessage: String = "Initialized"
)

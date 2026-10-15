package com.rideshield.phone.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import com.rideshield.phone.network.ConnectionStatus
import com.rideshield.phone.network.RswClientTelemetry
import com.rideshield.phone.protocol.RswRejectCode
import com.rideshield.phone.ui.theme.*

@Composable
fun ControllerHealthCard(
    telemetry: RswClientTelemetry
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(DarkSurface)
            .border(1.dp, DarkBorder, RoundedCornerShape(12.dp))
            .padding(12.dp)
    ) {
        // Title & Connection Status Badge
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(text = "NodeMCU ESP-32S Safety Core (RSW1)", style = Typography.titleMedium)

            val statusColor = when (telemetry.status) {
                ConnectionStatus.CONNECTED_ACTIVE -> StatusGreen
                ConnectionStatus.AUTHENTICATING, ConnectionStatus.SYNCING, ConnectionStatus.CONNECTING -> StatusOrange
                ConnectionStatus.DISCONNECTED, ConnectionStatus.ERROR, ConnectionStatus.RECONNECTING -> StatusRed
            }
            Text(
                text = telemetry.status.name,
                color = statusColor,
                style = Typography.labelSmall,
                modifier = Modifier
                    .background(statusColor.copy(alpha = 0.15f), RoundedCornerShape(4.dp))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            )
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Controller State Machine & Active Warning Banner
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(8.dp))
                .background(DarkSurfaceVariant)
                .padding(10.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(text = "CONTROLLER STATE", style = Typography.labelSmall)
                val stateColor = when (telemetry.controllerState) {
                    "Monitor" -> StatusGreen
                    "Countdown", "Incident" -> StatusRed
                    "Verify", "Degraded" -> StatusOrange
                    else -> TextPrimary
                }
                Text(text = telemetry.controllerState, color = stateColor, style = Typography.titleMedium)
            }

            Column(horizontalAlignment = Alignment.End) {
                Text(text = "ACTIVE WARNING", style = Typography.labelSmall)
                val warnColor = if (telemetry.controllerWarning == "None") StatusGreen else StatusOrange
                Text(text = telemetry.controllerWarning, color = warnColor, style = Typography.titleMedium)
            }
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Freshness Indicators (front_valid / rear_valid from controller)
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(text = "FRONT OBSERVATION: ", style = Typography.bodySmall)
                Text(
                    text = if (telemetry.controllerFrontValid) "VALID & CONFIRMED" else "IDLE / EXPIRED",
                    color = if (telemetry.controllerFrontValid) StatusGreen else TextMuted,
                    style = Typography.bodySmall
                )
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(text = "REAR OBSERVATION: ", style = Typography.bodySmall)
                Text(
                    text = if (telemetry.controllerRearValid) "VALID" else "IDLE / EXPIRED",
                    color = if (telemetry.controllerRearValid) StatusGreen else TextMuted,
                    style = Typography.bodySmall
                )
            }
        }

        Spacer(modifier = Modifier.height(10.dp))

        // Clock Sync & Session Diagnostics Table
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Text(text = "SESSION ID", style = Typography.labelSmall)
                Text(text = "%08X".format(telemetry.sessionId), style = Typography.bodySmall)
            }
            Column {
                Text(text = "SEQUENCE", style = Typography.labelSmall)
                Text(text = "${telemetry.currentSequence}", style = Typography.bodySmall)
            }
            Column {
                Text(text = "CLOCK OFFSET", style = Typography.labelSmall)
                Text(text = "${telemetry.clockOffsetMs} ms", color = AccentCyan, style = Typography.bodySmall)
            }
            Column {
                Text(text = "UNCERTAINTY", style = Typography.labelSmall)
                val uncColor = if (telemetry.clockUncertaintyMs < 50) StatusGreen else StatusOrange
                Text(text = "±${telemetry.clockUncertaintyMs} ms", color = uncColor, style = Typography.bodySmall)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Packet Counter & Rejections
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Text(text = "SENT PACKETS", style = Typography.labelSmall)
                Text(text = "Total: ${telemetry.totalSentPackets} (Obs: ${telemetry.sentObservations})", style = Typography.bodySmall)
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(text = "REJECTED BY CONTROLLER", style = Typography.labelSmall)
                val rejectColor = if (telemetry.rejectedPackets == 0L) StatusGreen else StatusRed
                val rejectText = if (telemetry.rejectedPackets == 0L) "0 (None)" else "${telemetry.rejectedPackets} (${telemetry.lastRejectCode.name})"
                Text(text = rejectText, color = rejectColor, style = Typography.bodySmall)
            }
        }
    }
}

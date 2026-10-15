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
import com.rideshield.phone.ui.theme.*
import com.rideshield.phone.vision.ModelInfo
import com.rideshield.phone.vision.ModelStatus

@Composable
fun ModelStatusCard(
    modelInfo: ModelInfo,
    inferenceLatencyMs: Long,
    detectionCount: Int,
    highestConfidence: Int,
    isHazardActive: Boolean
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(DarkSurface)
            .border(1.dp, DarkBorder, RoundedCornerShape(12.dp))
            .padding(12.dp)
    ) {
        // Model Name & Status Pill
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(text = modelInfo.name, style = Typography.titleMedium)

            val (badgeText, badgeColor) = when (modelInfo.status) {
                ModelStatus.LOADED -> "LOADED" to StatusGreen
                ModelStatus.UNAVAILABLE -> "UNAVAILABLE" to StatusGrey
                ModelStatus.ERROR -> "ERROR" to StatusRed
            }

            Text(
                text = badgeText,
                color = badgeColor,
                style = Typography.labelSmall,
                modifier = Modifier
                    .background(badgeColor.copy(alpha = 0.15f), RoundedCornerShape(4.dp))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            )
        }

        Spacer(modifier = Modifier.height(4.dp))
        Text(text = "Version: ${modelInfo.version}", style = Typography.labelSmall)
        Spacer(modifier = Modifier.height(8.dp))

        if (modelInfo.status == ModelStatus.UNAVAILABLE) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(6.dp))
                    .background(DarkSurfaceVariant)
                    .padding(8.dp)
            ) {
                Text(
                    text = "Model unavailable. Pipeline strictly suppresses output; zero invented detections are emitted.",
                    style = Typography.bodySmall,
                    color = TextMuted
                )
            }
        } else {
            // Metrics row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column {
                    Text(text = "LATENCY", style = Typography.labelSmall)
                    Text(text = "$inferenceLatencyMs ms", color = AccentCyan, style = Typography.bodySmall)
                }
                Column {
                    Text(text = "DETECTIONS", style = Typography.labelSmall)
                    Text(text = "$detectionCount", color = TextPrimary, style = Typography.bodySmall)
                }
                Column {
                    Text(text = "CONFIDENCE", style = Typography.labelSmall)
                    Text(text = "$highestConfidence %", color = if (highestConfidence >= 60) StatusOrange else TextSecondary, style = Typography.bodySmall)
                }
                Column {
                    Text(text = "HAZARD FLAG", style = Typography.labelSmall)
                    val flagColor = if (isHazardActive) StatusOrange else StatusGreen
                    val flagText = if (isHazardActive) "TRIGGERED" else "CLEAR"
                    Text(text = flagText, color = flagColor, style = Typography.bodySmall)
                }
            }
        }
    }
}

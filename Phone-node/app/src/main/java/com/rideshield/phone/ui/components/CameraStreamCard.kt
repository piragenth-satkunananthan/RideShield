package com.rideshield.phone.ui.components

import android.graphics.Bitmap
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.rideshield.phone.camera.StreamQualityStats
import com.rideshield.phone.ui.theme.*
import com.rideshield.phone.vision.BoundingBox

@Composable
fun CameraStreamCard(
    title: String,
    bitmap: Bitmap?,
    detections: List<BoundingBox>,
    stats: StreamQualityStats,
    isAvailable: Boolean = true,
    unavailableReason: String = "UNAVAILABLE"
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(DarkSurface)
            .border(1.dp, DarkBorder, RoundedCornerShape(12.dp))
            .padding(12.dp)
    ) {
        // Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(text = title, style = Typography.titleMedium)
            val badgeColor = when {
                !isAvailable -> StatusGrey
                stats.isStreamHealthy -> StatusGreen
                stats.fps > 0 -> StatusOrange
                else -> StatusRed
            }
            val badgeText = when {
                !isAvailable -> unavailableReason
                stats.isStreamHealthy -> "LIVE (${stats.fps.toInt()} FPS)"
                stats.fps > 0 -> "DEGRADED (${stats.fps.toInt()} FPS)"
                else -> "OFFLINE"
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

        Spacer(modifier = Modifier.height(8.dp))

        // Video / Frame Canvas
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(200.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(Color.Black),
            contentAlignment = Alignment.Center
        ) {
            if (!isAvailable) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(text = "CAMERA NODE UNAVAILABLE", color = StatusGrey, style = Typography.titleMedium)
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(text = "Firmware stream not connected", color = TextMuted, style = Typography.bodySmall)
                }
            } else if (bitmap != null) {
                // Draw camera image
                Image(
                    bitmap = bitmap.asImageBitmap(),
                    contentDescription = title,
                    modifier = Modifier.fillMaxSize()
                )

                // Overlay bounding boxes
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val canvasW = size.width
                    val canvasH = size.height
                    val imgW = bitmap.width.toFloat()
                    val imgH = bitmap.height.toFloat()

                    if (imgW > 0 && imgH > 0) {
                        for (det in detections) {
                            val x1 = (det.rect.left / imgW) * canvasW
                            val y1 = (det.rect.top / imgH) * canvasH
                            val x2 = (det.rect.right / imgW) * canvasW
                            val y2 = (det.rect.bottom / imgH) * canvasH

                            val boxColor = if (det.confidence >= 0.60f) StatusOrange else AccentCyan
                            drawRect(
                                color = boxColor,
                                topLeft = Offset(x1, y1),
                                size = Size(x2 - x1, y2 - y1),
                                style = Stroke(width = 3.dp.toPx())
                            )
                        }
                    }
                }
            } else {
                Text(text = "Awaiting video stream...", color = TextMuted, style = Typography.bodyMedium)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Diagnostic Stream Stats Row
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Text(text = "STREAM AGE", style = Typography.labelSmall)
                val ageColor = if (stats.streamAgeMs < 300) StatusGreen else if (stats.streamAgeMs < 500) StatusOrange else StatusRed
                Text(text = "${stats.streamAgeMs} ms", color = ageColor, style = Typography.bodySmall)
            }
            Column {
                Text(text = "SEQUENCE GAPS", style = Typography.labelSmall)
                Text(text = "${stats.sequenceGaps}", color = if (stats.sequenceGaps == 0L) TextSecondary else StatusOrange, style = Typography.bodySmall)
            }
            Column {
                Text(text = "QUEUE DROPS", style = Typography.labelSmall)
                Text(text = "${stats.droppedFrames}", color = TextSecondary, style = Typography.bodySmall)
            }
            Column {
                Text(text = "RESOLUTION", style = Typography.labelSmall)
                Text(text = "${stats.lastFrameWidth}x${stats.lastFrameHeight}", color = TextSecondary, style = Typography.bodySmall)
            }
        }
    }
}

package com.rideshield.phone.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.rideshield.phone.network.SensorTelemetry
import com.rideshield.phone.ui.theme.*

@Composable
fun SensorTelemetryCard(telemetry: SensorTelemetry) {
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = DarkSurface)) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text("Helmet sensors", style = Typography.titleMedium)
            Text(telemetry.message, color = if (telemetry.sensors == null) StatusOrange else AccentCyan)
            telemetry.sensors?.values?.forEach { (label, value) -> Text("$label: $value", style = Typography.bodySmall) }
            Text("IR and MQ readings are experimental signals, not medical measurements.", style = Typography.bodySmall, color = TextMuted)
        }
    }
}

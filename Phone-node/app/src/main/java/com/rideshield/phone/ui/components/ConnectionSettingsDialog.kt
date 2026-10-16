package com.rideshield.phone.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.rideshield.phone.network.RswWebSocketClient
import androidx.compose.ui.text.input.PasswordVisualTransformation
import com.rideshield.phone.ui.theme.*

@Composable
fun ConnectionSettingsDialog(
    currentWsUrl: String,
    currentToken: String,
    currentFrontUrl: String,
    currentRearUrl: String,
    isMockEnabled: Boolean,
    onDismiss: () -> Unit,
    onSave: (wsUrl: String, token: String, frontUrl: String, rearUrl: String, mock: Boolean) -> Unit
) {
    var wsUrl by remember { mutableStateOf(currentWsUrl) }
    var token by remember { mutableStateOf(currentToken) }
    var frontUrl by remember { mutableStateOf(currentFrontUrl) }
    var rearUrl by remember { mutableStateOf(currentRearUrl) }
    var mock by remember { mutableStateOf(isMockEnabled) }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            shape = RoundedCornerShape(16.dp),
            color = DarkSurface,
            border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder),
            modifier = Modifier.fillMaxWidth().padding(16.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(text = "Connection & Bench Settings", style = Typography.titleMedium)
                Spacer(modifier = Modifier.height(12.dp))

                OutlinedTextField(
                    value = wsUrl,
                    onValueChange = { wsUrl = it },
                    label = { Text("NodeMCU-32S WebSocket URL") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary
                    )
                )

                Spacer(modifier = Modifier.height(8.dp))

                OutlinedTextField(
                    value = token,
                    onValueChange = { if (it.length <= 32) token = it },
                    label = { Text("Pre-Shared Token (32 ASCII bytes)") },
                    supportingText = { Text("${token.length}/32 ASCII characters", style = Typography.labelSmall) },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary
                    )
                )

                Spacer(modifier = Modifier.height(8.dp))

                OutlinedTextField(
                    value = frontUrl,
                    onValueChange = { frontUrl = it },
                    label = { Text("Front Camera Stream URL") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary
                    )
                )

                Spacer(modifier = Modifier.height(8.dp))

                OutlinedTextField(
                    value = rearUrl,
                    onValueChange = { rearUrl = it },
                    label = { Text("Rear Camera Stream URL") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary
                    )
                )

                Spacer(modifier = Modifier.height(12.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(text = "Bench Simulator Mode", style = Typography.titleMedium)
                        Text(
                            text = "Local previews only; never sent to helmet",
                            style = Typography.bodySmall,
                            color = TextMuted
                        )
                    }
                    Switch(
                        checked = mock,
                        onCheckedChange = { mock = it }
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End
                ) {
                    TextButton(onClick = onDismiss) {
                        Text("Cancel", color = TextSecondary)
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    Button(
                        enabled = RswWebSocketClient.validSettings(wsUrl, token) &&
                            (frontUrl.startsWith("http://") || frontUrl.startsWith("https://")) &&
                            (rearUrl.startsWith("http://") || rearUrl.startsWith("https://")),
                        onClick = {
                            onSave(wsUrl, token, frontUrl, rearUrl, mock)
                            onDismiss()
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = AccentCyan)
                    ) {
                        Text("Apply", color = DarkBackground)
                    }
                }
            }
        }
    }
}

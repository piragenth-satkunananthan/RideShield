package com.rideshield.phone.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Power
import androidx.compose.material.icons.filled.PowerOff
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import com.rideshield.phone.network.ConnectionStatus
import com.rideshield.phone.safety.SafetyBoundaryGuard
import com.rideshield.phone.ui.MainViewModel
import com.rideshield.phone.ui.components.CameraStreamCard
import com.rideshield.phone.ui.components.ConnectionSettingsDialog
import com.rideshield.phone.ui.components.ControllerHealthCard
import com.rideshield.phone.ui.components.ModelStatusCard
import com.rideshield.phone.ui.components.SensorTelemetryCard
import com.rideshield.phone.ui.theme.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(viewModel: MainViewModel) {
    val telemetry by viewModel.clientTelemetry.collectAsState()
    val sensors by viewModel.sensorTelemetry.collectAsState()
    val connectionRequested by viewModel.connectionRequested.collectAsState()
    val connectionMessage by viewModel.connectionMessage.collectAsState()
    val cameraEnabled by viewModel.cameraEnabled.collectAsState()
    val frontStats by viewModel.frontCameraStats.collectAsState()
    val rearStats by viewModel.rearCameraStats.collectAsState()
    val frontVision by viewModel.frontVision.collectAsState()
    val rearVision by viewModel.rearVision.collectAsState()

    val wsUrl by viewModel.wsUrl.collectAsState()
    val token by viewModel.token.collectAsState()
    val frontUrl by viewModel.frontCameraUrl.collectAsState()
    val rearUrl by viewModel.rearCameraUrl.collectAsState()
    val isMock by viewModel.isMockMode.collectAsState()
    val showSettings by viewModel.showSettingsDialog.collectAsState()

    val scrollState = rememberScrollState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(text = "RideShield HUD", style = Typography.titleLarge)
                        Text(
                            text = if (isMock) "BENCH SIMULATION MODE" else "HARDWARE AP MODE",
                            style = Typography.labelSmall,
                            color = if (isMock) StatusOrange else AccentCyan
                        )
                    }
                },
                actions = {
                    // Connect / Disconnect button
                    val isConnected = connectionRequested
                    IconButton(onClick = {
                        if (isConnected) {
                            viewModel.disconnectHelmetController()
                        } else {
                            viewModel.connectHelmetController()
                        }
                    }) {
                        Icon(
                            imageVector = if (isConnected) Icons.Default.PowerOff else Icons.Default.Power,
                            contentDescription = if (isConnected) "Disconnect" else "Connect",
                            tint = if (isConnected) StatusGreen else StatusOrange
                        )
                    }

                    // Settings Dialog trigger
                    IconButton(onClick = { viewModel.openSettings() }) {
                        Icon(
                            imageVector = Icons.Default.Settings,
                            contentDescription = "Settings",
                            tint = TextSecondary
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = DarkBackground,
                    titleContentColor = TextPrimary
                )
            )
        },
        containerColor = DarkBackground
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 16.dp)
                .verticalScroll(scrollState)
        ) {
            Spacer(modifier = Modifier.height(8.dp))

            // Safety Boundary Guard Architecture Notice
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(DarkSurfaceVariant)
                    .padding(10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "SAFETY CORE NOTICE: Crash detection, SOS SMS, and warning buzzers stay strictly autonomous on the NodeMCU ESP-32S microcontroller.",
                    style = Typography.labelSmall,
                    color = AccentCyan
                )
            }

            Spacer(modifier = Modifier.height(12.dp))

            // ESP32-S3 Safety Core Status Card
            Text(connectionMessage, style = Typography.bodySmall, color = TextSecondary)
            Text(telemetry.statusMessage, style = Typography.bodySmall, color = TextMuted)
            ControllerHealthCard(telemetry = telemetry)
            Spacer(modifier = Modifier.height(12.dp))
            SensorTelemetryCard(sensors)
            TextButton(onClick = viewModel::toggleCameras) { Text(if (cameraEnabled) "Stop camera previews" else "Start camera previews") }
            if (isMock) Text("Simulation stays on this phone; synthetic observations are not sent.", color = StatusOrange)

            Spacer(modifier = Modifier.height(16.dp))

            // Live Camera Previews Section Header
            Text(text = "Camera Streams & Perception", style = Typography.titleMedium)
            Spacer(modifier = Modifier.height(8.dp))

            // Front Camera View
            CameraStreamCard(
                title = "Front Camera (Pothole & Road Feed)",
                bitmap = frontVision.latestBitmap,
                detections = frontVision.potholes,
                stats = frontStats,
                isAvailable = cameraEnabled && frontStats.fps > 0
            )

            Spacer(modifier = Modifier.height(12.dp))

            // Rear Camera View
            CameraStreamCard(
                title = "Rear Camera (Blind Spot & Vehicle Feed)",
                bitmap = rearVision.latestBitmap,
                detections = rearVision.vehicles,
                stats = rearStats,
                isAvailable = cameraEnabled && rearStats.fps > 0,
                unavailableReason = "REAR NODE UNAVAILABLE"
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Vision Models Status Section Header
            Text(text = "Vision Model Pipelines", style = Typography.titleMedium)
            Spacer(modifier = Modifier.height(8.dp))

            // 1. Pothole Model Card
            ModelStatusCard(
                modelInfo = viewModel.potholeDetector.getModelInfo(),
                inferenceLatencyMs = frontVision.potholeLatencyMs,
                detectionCount = frontVision.potholes.size,
                highestConfidence = frontVision.highestConfidence,
                isHazardActive = frontVision.isPotholeHazard
            )

            Spacer(modifier = Modifier.height(8.dp))

            // 2. Wet Road Classifier Card (UNAVAILABLE)
            ModelStatusCard(
                modelInfo = viewModel.wetRoadClassifier.getModelInfo(),
                inferenceLatencyMs = 0L,
                detectionCount = 0,
                highestConfidence = 0,
                isHazardActive = false
            )

            Spacer(modifier = Modifier.height(8.dp))

            // 3. Rear Vehicle Model Card (UNAVAILABLE)
            ModelStatusCard(
                modelInfo = viewModel.rearVehicleDetector.getModelInfo(),
                inferenceLatencyMs = 0L,
                detectionCount = 0,
                highestConfidence = 0,
                isHazardActive = false
            )

            Spacer(modifier = Modifier.height(24.dp))
        }

        if (showSettings) {
            ConnectionSettingsDialog(
                currentWsUrl = wsUrl,
                currentToken = token,
                currentFrontUrl = frontUrl,
                currentRearUrl = rearUrl,
                isMockEnabled = isMock,
                onDismiss = { viewModel.closeSettings() },
                onSave = { newWs, newTok, newFront, newRear, mock ->
                    viewModel.saveSettings(newWs, newTok, newFront, newRear, mock)
                }
            )
        }
    }
}

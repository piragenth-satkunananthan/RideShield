package com.rideshield.phone.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.rideshield.phone.camera.CameraStreamClient
import com.rideshield.phone.camera.StreamQualityStats
import com.rideshield.phone.network.ConnectionStatus
import com.rideshield.phone.network.RswClientTelemetry
import com.rideshield.phone.network.RswWebSocketClient
import com.rideshield.phone.network.ControllerTelemetryClient
import com.rideshield.phone.network.WifiApManager
import com.rideshield.phone.protocol.RswConstants
import com.rideshield.phone.vision.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class MainViewModel(application: Application) : AndroidViewModel(application) {

    val webSocketClient = RswWebSocketClient()
    private val sensorClient = ControllerTelemetryClient()
    val sensorTelemetry = sensorClient.telemetry
    private val wifi = WifiApManager(application)
    private val preferences = application.getSharedPreferences("controller", 0)
    private val _connectionRequested = MutableStateFlow(false)
    val connectionRequested = _connectionRequested.asStateFlow()
    private val _connectionMessage = MutableStateFlow("Join RideShield Wi-Fi in Android settings, then tap Connect")
    val connectionMessage = _connectionMessage.asStateFlow()
    private val _cameraEnabled = MutableStateFlow(false)
    val cameraEnabled = _cameraEnabled.asStateFlow()

    val frontCameraClient = CameraStreamClient(nodeName = "FRONT")
    val rearCameraClient = CameraStreamClient(nodeName = "REAR")

    // Missing weights never produce synthetic hardware observations.
    private val onnxPotholeDetector = OnnxPotholeDetector(context = application)
    val potholeDetector: PotholeDetector = if (onnxPotholeDetector.initialize()) {
        onnxPotholeDetector
    } else {
        UnavailablePotholeDetector()
    }

    val wetRoadClassifier = UnavailableWetRoadClassifier()
    val rearVehicleDetector = UnavailableRearVehicleDetector()

    val visionPipeline = VisionPipelineManager(
        frontCameraClient = frontCameraClient,
        rearCameraClient = rearCameraClient,
        webSocketClient = webSocketClient,
        potholeDetector = potholeDetector,
        wetRoadClassifier = wetRoadClassifier,
        rearVehicleDetector = rearVehicleDetector
    )

    // UI State
    val clientTelemetry: StateFlow<RswClientTelemetry> = webSocketClient.telemetry
    val frontCameraStats: StateFlow<StreamQualityStats> = frontCameraClient.stats
    val rearCameraStats: StateFlow<StreamQualityStats> = rearCameraClient.stats
    val frontVision: StateFlow<FrontVisionTelemetry> = visionPipeline.frontTelemetry
    val rearVision: StateFlow<RearVisionTelemetry> = visionPipeline.rearTelemetry

    // Configuration settings state
    private val _wsUrl = MutableStateFlow(preferences.getString("ws", RswConstants.DEFAULT_CONTROLLER_URL)!!)
    val wsUrl: StateFlow<String> = _wsUrl.asStateFlow()

    private val _token = MutableStateFlow(preferences.getString("token", RswConstants.DEFAULT_TOKEN)!!)
    val token: StateFlow<String> = _token.asStateFlow()

    private val _frontCameraUrl = MutableStateFlow(preferences.getString("front", RswConstants.DEFAULT_FRONT_CAMERA_URL)!!)
    val frontCameraUrl: StateFlow<String> = _frontCameraUrl.asStateFlow()

    private val _rearCameraUrl = MutableStateFlow(preferences.getString("rear", RswConstants.DEFAULT_REAR_CAMERA_URL)!!)
    val rearCameraUrl: StateFlow<String> = _rearCameraUrl.asStateFlow()

    private val _isMockMode = MutableStateFlow(false) // Hardware default; simulation is an explicit local preview.
    val isMockMode: StateFlow<Boolean> = _isMockMode.asStateFlow()

    private val _showSettingsDialog = MutableStateFlow(false)
    val showSettingsDialog: StateFlow<Boolean> = _showSettingsDialog.asStateFlow()

    init {
        viewModelScope.launch {
            clientTelemetry.collect { status ->
                if (status.status == ConnectionStatus.CONNECTED_ACTIVE) {
                    if (sensorClient.telemetry.value.message == "Disconnected") sensorClient.start(_wsUrl.value)
                } else sensorClient.stop()
            }
        }
    }
    fun toggleCameras() {
        if (_cameraEnabled.value) {
            frontCameraClient.stop(); rearCameraClient.stop(); _cameraEnabled.value = false
        } else startPipeline()
    }

    fun startPipeline() {
        _cameraEnabled.value = true
        visionPipeline.start()
        frontCameraClient.start(url = _frontCameraUrl.value, useMock = _isMockMode.value)
        rearCameraClient.start(url = _rearCameraUrl.value, useMock = _isMockMode.value)
    }

    fun connectHelmetController() {
        if (!RswWebSocketClient.validSettings(_wsUrl.value, _token.value)) {
            _connectionMessage.value = "Correct the URL and 32-character ASCII token in Settings"; return
        }
        _connectionRequested.value = true; _connectionMessage.value = "Waiting for Wi-Fi…"
        wifi.bindToHelmetWifi(
            onConnected = {
                _connectionMessage.value = "Wi-Fi selected; authenticating controller"
                webSocketClient.connect(_wsUrl.value, _token.value)
            },
            onError = { message ->
                _connectionMessage.value = message; _connectionRequested.value = false
                webSocketClient.disconnect(); sensorClient.stop()
            },
            onLost = {
                _connectionMessage.value = "Wi-Fi lost; waiting for it to return"
                webSocketClient.disconnect(); sensorClient.stop()
            }
        )
    }

    fun disconnectHelmetController() {
        _connectionRequested.value = false
        wifi.unbind(); webSocketClient.disconnect(); sensorClient.stop()
        _connectionMessage.value = "Disconnected"
    }

    fun openSettings() {
        _showSettingsDialog.value = true
    }

    fun closeSettings() {
        _showSettingsDialog.value = false
    }

    fun saveSettings(newWsUrl: String, newToken: String, newFrontUrl: String, newRearUrl: String, mock: Boolean) {
        if (!RswWebSocketClient.validSettings(newWsUrl, newToken)) return
        preferences.edit().putString("ws", newWsUrl).putString("token", newToken)
            .putString("front", newFrontUrl).putString("rear", newRearUrl).apply()
        _wsUrl.value = newWsUrl
        _token.value = newToken
        _frontCameraUrl.value = newFrontUrl
        _rearCameraUrl.value = newRearUrl
        _isMockMode.value = mock

        // Restart camera streams with updated endpoints or mock switch
        frontCameraClient.stop()
        rearCameraClient.stop()

        if (_cameraEnabled.value) {
            frontCameraClient.start(url = newFrontUrl, useMock = mock)
            rearCameraClient.start(url = newRearUrl, useMock = mock)
        }

        // If connected, reconnect with updated URL/token
        if (_connectionRequested.value) { disconnectHelmetController(); connectHelmetController() }
    }

    override fun onCleared() {
        super.onCleared()
        visionPipeline.stop()
        frontCameraClient.stop()
        rearCameraClient.stop()
        disconnectHelmetController()
    }
}

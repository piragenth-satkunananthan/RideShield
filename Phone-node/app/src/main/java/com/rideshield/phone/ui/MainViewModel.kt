package com.rideshield.phone.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.rideshield.phone.camera.CameraStreamClient
import com.rideshield.phone.camera.StreamQualityStats
import com.rideshield.phone.network.ConnectionStatus
import com.rideshield.phone.network.RswClientTelemetry
import com.rideshield.phone.network.RswWebSocketClient
import com.rideshield.phone.protocol.RswConstants
import com.rideshield.phone.vision.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class MainViewModel(application: Application) : AndroidViewModel(application) {

    val webSocketClient = RswWebSocketClient()

    val frontCameraClient = CameraStreamClient(nodeName = "FRONT")
    val rearCameraClient = CameraStreamClient(nodeName = "REAR")

    // Pothole detector: attempts ONNX first, falls back to Mock if weights missing
    private val onnxPotholeDetector = OnnxPotholeDetector(context = application)
    private val mockPotholeDetector = MockPotholeDetector()
    val potholeDetector: PotholeDetector = if (onnxPotholeDetector.initialize()) {
        onnxPotholeDetector
    } else {
        mockPotholeDetector
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
    private val _wsUrl = MutableStateFlow(RswConstants.DEFAULT_CONTROLLER_URL)
    val wsUrl: StateFlow<String> = _wsUrl.asStateFlow()

    private val _token = MutableStateFlow(RswConstants.DEFAULT_TOKEN)
    val token: StateFlow<String> = _token.asStateFlow()

    private val _frontCameraUrl = MutableStateFlow(RswConstants.DEFAULT_FRONT_CAMERA_URL)
    val frontCameraUrl: StateFlow<String> = _frontCameraUrl.asStateFlow()

    private val _rearCameraUrl = MutableStateFlow(RswConstants.DEFAULT_REAR_CAMERA_URL)
    val rearCameraUrl: StateFlow<String> = _rearCameraUrl.asStateFlow()

    private val _isMockMode = MutableStateFlow(true) // Default to mock mode for out-of-the-box demo
    val isMockMode: StateFlow<Boolean> = _isMockMode.asStateFlow()

    private val _showSettingsDialog = MutableStateFlow(false)
    val showSettingsDialog: StateFlow<Boolean> = _showSettingsDialog.asStateFlow()

    init {
        startPipeline()
    }

    fun startPipeline() {
        visionPipeline.start()
        frontCameraClient.start(url = _frontCameraUrl.value, useMock = _isMockMode.value)
        rearCameraClient.start(url = _rearCameraUrl.value, useMock = _isMockMode.value)
    }

    fun connectHelmetController() {
        webSocketClient.connect(url = _wsUrl.value, token = _token.value)
    }

    fun disconnectHelmetController() {
        webSocketClient.disconnect()
    }

    fun openSettings() {
        _showSettingsDialog.value = true
    }

    fun closeSettings() {
        _showSettingsDialog.value = false
    }

    fun saveSettings(newWsUrl: String, newToken: String, newFrontUrl: String, newRearUrl: String, mock: Boolean) {
        _wsUrl.value = newWsUrl
        _token.value = newToken
        _frontCameraUrl.value = newFrontUrl
        _rearCameraUrl.value = newRearUrl
        _isMockMode.value = mock

        // Restart camera streams with updated endpoints or mock switch
        frontCameraClient.stop()
        rearCameraClient.stop()

        frontCameraClient.start(url = newFrontUrl, useMock = mock)
        rearCameraClient.start(url = newRearUrl, useMock = mock)

        // If connected, reconnect with updated URL/token
        if (clientTelemetry.value.status != ConnectionStatus.DISCONNECTED) {
            webSocketClient.disconnect()
            webSocketClient.connect(url = newWsUrl, token = newToken)
        }
    }

    override fun onCleared() {
        super.onCleared()
        visionPipeline.stop()
        frontCameraClient.stop()
        rearCameraClient.stop()
        webSocketClient.disconnect()
    }
}

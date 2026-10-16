package com.rideshield.phone.vision

import android.graphics.Bitmap
import android.os.SystemClock
import android.util.Log
import com.rideshield.phone.camera.CameraFrame
import com.rideshield.phone.camera.CameraStreamClient
import com.rideshield.phone.network.RswWebSocketClient
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.atomic.AtomicBoolean

data class FrontVisionTelemetry(
    val latestBitmap: Bitmap? = null,
    val potholes: List<BoundingBox> = emptyList(),
    val highestConfidence: Int = 0,
    val potholeLatencyMs: Long = 0L,
    val wetRoadStatus: ModelStatus = ModelStatus.UNAVAILABLE,
    val potholeStatus: ModelStatus = ModelStatus.LOADED,
    val isPotholeHazard: Boolean = false,
    val isWetHazard: Boolean = false,
    val frameCapturePhoneMs: Long = 0L,
    val cameraClockUncertaintyMs: Long = 0L
)

data class RearVisionTelemetry(
    val latestBitmap: Bitmap? = null,
    val vehicles: List<BoundingBox> = emptyList(),
    val highestConfidence: Int = 0,
    val vehicleLatencyMs: Long = 0L,
    val rearModelStatus: ModelStatus = ModelStatus.UNAVAILABLE,
    val isRearHazard: Boolean = false,
    val frameCapturePhoneMs: Long = 0L,
    val cameraClockUncertaintyMs: Long = 0L
)

/**
 * Orchestrates camera stream ingest, ML inference workers, bounding box tracking,
 * and dispatch of validated fresh observations to the RSW1 WebSocket client.
 */
class VisionPipelineManager(
    val frontCameraClient: CameraStreamClient,
    val rearCameraClient: CameraStreamClient,
    val webSocketClient: RswWebSocketClient,
    val potholeDetector: PotholeDetector,
    val wetRoadClassifier: WetRoadClassifier = UnavailableWetRoadClassifier(),
    val rearVehicleDetector: RearVehicleDetectorTracker = UnavailableRearVehicleDetector(),
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() },
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())
) {
    private val tag = "VisionPipelineManager"
    private val isRunning = AtomicBoolean(false)

    private val _frontTelemetry = MutableStateFlow(FrontVisionTelemetry())
    val frontTelemetry: StateFlow<FrontVisionTelemetry> = _frontTelemetry.asStateFlow()

    private val _rearTelemetry = MutableStateFlow(RearVisionTelemetry())
    val rearTelemetry: StateFlow<RearVisionTelemetry> = _rearTelemetry.asStateFlow()

    private var frontWorkerJob: Job? = null
    private var rearWorkerJob: Job? = null

    fun start() {
        if (isRunning.getAndSet(true)) return

        potholeDetector.initialize()
        wetRoadClassifier.initialize()
        rearVehicleDetector.initialize()

        startFrontWorker()
        startRearWorker()
    }

    fun stop() {
        isRunning.set(false)
        frontWorkerJob?.cancel()
        rearWorkerJob?.cancel()
        frontWorkerJob = null
        rearWorkerJob = null

        potholeDetector.close()
        wetRoadClassifier.close()
        rearVehicleDetector.close()
    }

    private fun startFrontWorker() {
        frontWorkerJob = coroutineScope.launch {
            Log.i(tag, "Front vision worker started")
            while (isActive && isRunning.get()) {
                val frame: CameraFrame? = frontCameraClient.boundedQueue.take(timeoutMs = 100L)
                if (frame == null) {
                    continue
                }

                val decodeStart = clockProvider()
                val bitmap = frame.decodeBitmap()
                val decodeDuration = clockProvider() - decodeStart
                frontCameraClient.qualityMonitor.onFrameDecoded(bitmap, decodeDuration)

                if (bitmap == null) {
                    Log.w(tag, "Failed to decode front JPEG frame #${frame.frameIndex}")
                    continue
                }

                // 1. Run Pothole inference on front frame
                val potholeResult = potholeDetector.detect(bitmap)

                // 2. Run Wet-Road classification on front frame (reports UNAVAILABLE, never invents)
                val wetRoadResult = wetRoadClassifier.classify(bitmap)

                // Update UI Telemetry
                _frontTelemetry.value = FrontVisionTelemetry(
                    latestBitmap = bitmap,
                    potholes = potholeResult.potholes,
                    highestConfidence = potholeResult.highestConfidence,
                    potholeLatencyMs = potholeResult.inferenceDurationMs,
                    wetRoadStatus = wetRoadResult.status,
                    potholeStatus = potholeResult.status,
                    isPotholeHazard = potholeResult.isHazardPresent,
                    isWetHazard = wetRoadResult.isWetRoad,
                    frameCapturePhoneMs = frame.mappedPhoneCaptureMs,
                    cameraClockUncertaintyMs = frame.clockUncertaintyMs
                )

                // 3. Dispatch Observation to controller if WebSocket is active
                // Confidence sent to controller: use the highest detected hazard confidence, or 0 if clear
                val confidence = if (potholeResult.isHazardPresent) {
                    potholeResult.highestConfidence
                } else if (wetRoadResult.isWetRoad) {
                    wetRoadResult.confidence
                } else {
                    // When clear, send confidence 0 or model's baseline
                    0
                }

                if (!frame.synthetic && !frontCameraClient.isMockMode && potholeResult.status == ModelStatus.LOADED) webSocketClient.sendFrontObservation(
                    capturePhoneMs = frame.mappedPhoneCaptureMs,
                    cameraUncertaintyMs = frame.clockUncertaintyMs,
                    pothole = potholeResult.isHazardPresent,
                    wetRoad = wetRoadResult.isWetRoad,
                    confidence = confidence
                )
            }
        }
    }

    private fun startRearWorker() {
        rearWorkerJob = coroutineScope.launch {
            Log.i(tag, "Rear vision worker started")
            while (isActive && isRunning.get()) {
                val frame: CameraFrame? = rearCameraClient.boundedQueue.take(timeoutMs = 100L)
                if (frame == null) {
                    continue
                }

                val decodeStart = clockProvider()
                val bitmap = frame.decodeBitmap()
                val decodeDuration = clockProvider() - decodeStart
                rearCameraClient.qualityMonitor.onFrameDecoded(bitmap, decodeDuration)

                if (bitmap == null) {
                    continue
                }

                // Run rear vehicle detector / tracker (returns UNAVAILABLE, no invented detections)
                val rearResult = rearVehicleDetector.detectAndTrack(bitmap)

                _rearTelemetry.value = RearVisionTelemetry(
                    latestBitmap = bitmap,
                    vehicles = rearResult.detectedVehicles,
                    highestConfidence = rearResult.highestConfidence,
                    vehicleLatencyMs = rearResult.inferenceDurationMs,
                    rearModelStatus = rearResult.status,
                    isRearHazard = rearResult.isHazardPresent,
                    frameCapturePhoneMs = frame.mappedPhoneCaptureMs,
                    cameraClockUncertaintyMs = frame.clockUncertaintyMs
                )

                // Only dispatch rear observation if model is active and valid (or clear frames)
                if (!frame.synthetic && !rearCameraClient.isMockMode && rearResult.status == ModelStatus.LOADED) {
                    webSocketClient.sendRearObservation(
                        capturePhoneMs = frame.mappedPhoneCaptureMs,
                        cameraUncertaintyMs = frame.clockUncertaintyMs,
                        rearVehicle = rearResult.isHazardPresent,
                        confidence = rearResult.highestConfidence,
                        zone = rearResult.zone
                    )
                }
            }
        }
    }
}

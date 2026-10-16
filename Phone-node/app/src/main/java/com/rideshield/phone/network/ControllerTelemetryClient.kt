package com.rideshield.phone.network

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import okhttp3.*
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/** No sample means unavailable; failures clear old readings. */
data class ControllerSensors(val values: Map<String, String>) {
    companion object {
        fun parse(text: String): ControllerSensors {
            val j = JSONObject(text)
            require(j.opt("uptime_ms") is Number && j.opt("state") is String) { "Not controller telemetry" }
            val values = linkedMapOf<String, String>()
            for ((key, label) in listOf("imu" to "IMU healthy", "accel_g" to "Acceleration (g)", "gyro_dps" to "Rotation (°/s)",
                "head" to "Head", "wear" to "Wear / strap", "tof_valid" to "ToF valid", "ir_raw" to "IR raw",
                "eye_signal" to "Prolonged IR signal", "mq_mv" to "MQ input (mV)", "alcohol" to "MQ screening",
                "gps_fresh" to "Fresh GPS fix", "sd_ready" to "SD ready", "modem_online" to "Modem online",
                "sms_enabled" to "Live SMS enabled", "remaining_ms" to "Countdown (ms)", "gaps" to "Motion gaps", "free_heap" to "Free heap (bytes)")) {
                val v = j.opt(key)
                values[label] = if (v is Boolean || v is String || (v is Number && v.toDouble().isFinite())) v.toString() else "Unavailable"
            }
            values["Distance (mm)"] = if (j.opt("tof_valid") == true) number(j, "tof_mm") else "Unavailable"
            values["GPS location"] = if (j.opt("gps_fresh") == true) "${number(j,"latitude")}, ${number(j,"longitude")}" else "No fresh fix"
            return ControllerSensors(values)
        }
        private fun number(j: JSONObject, key: String) = (j.opt(key) as? Number)?.takeIf { it.toDouble().isFinite() }?.toString() ?: "Unavailable"
    }
}
data class SensorTelemetry(val sensors: ControllerSensors? = null, val message: String = "Disconnected")
class ControllerTelemetryClient(
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.IO + SupervisorJob()),
    private val client: Call.Factory = OkHttpClient.Builder().connectTimeout(1500, TimeUnit.MILLISECONDS)
        .readTimeout(1500, TimeUnit.MILLISECONDS).callTimeout(2000, TimeUnit.MILLISECONDS).build()
) {
    private val state = MutableStateFlow(SensorTelemetry())
    val telemetry = state.asStateFlow()
    private var generation = 0L
    private var job: Job? = null
    private var call: Call? = null
    @Synchronized fun start(webSocketUrl: String) {
        stop(); val endpoint = endpoint(webSocketUrl); val attempt = generation
        state.value = SensorTelemetry(message = "Waiting for sensor telemetry")
        job = scope.launch {
            while (isActive) {
                val request = client.newCall(Request.Builder().url(endpoint).build())
                synchronized(this@ControllerTelemetryClient) { if (attempt != generation) return@launch; call = request }
                val sample = try {
                    request.execute().use { response ->
                        check(response.isSuccessful)
                        val source = checkNotNull(response.body).source()
                        source.request(16385); check(source.buffer.size <= 16384)
                        ControllerSensors.parse(source.readUtf8())
                    }
                } catch (e: Exception) { null }
                synchronized(this@ControllerTelemetryClient) {
                    if (attempt != generation) return@launch
                    call = null
                    state.value = if (sample != null) SensorTelemetry(sample, "Live sensor telemetry")
                        else SensorTelemetry(message = "Telemetry unavailable; old readings cleared")
                }
                delay(1000)
            }
        }
    }
    @Synchronized fun stop() { generation++; call?.cancel(); call = null; job?.cancel(); job = null; state.value = SensorTelemetry() }
    companion object {
        fun endpoint(ws: String): HttpUrl {
            val parsed = Request.Builder().url(ws).build().url
            return parsed.newBuilder().port(if (parsed.isHttps) 443 else 80).encodedPath("/api/status").query(null).fragment(null).build()
        }
    }
}

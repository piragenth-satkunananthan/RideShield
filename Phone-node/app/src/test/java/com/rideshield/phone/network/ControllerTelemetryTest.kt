package com.rideshield.phone.network

import org.junit.Assert.*
import org.junit.Test
import kotlinx.coroutines.test.*
import okhttp3.*
import okhttp3.ResponseBody.Companion.toResponseBody
import okio.Timeout
import java.io.IOException

class ControllerTelemetryTest {
    @Test fun missingReadingsAreUnavailableAndStaleLocationIsHidden() {
        val s = ControllerSensors.parse("""{"uptime_ms":1000,"state":"MONITOR","imu":true,"tof_valid":false,"tof_mm":40,"gps_fresh":false,"latitude":1,"longitude":2}""")
        assertEquals("true", s.values["IMU healthy"])
        assertEquals("Unavailable", s.values["Distance (mm)"])
        assertEquals("No fresh fix", s.values["GPS location"])
        assertEquals("Unavailable", s.values["SD ready"])
    }
    @Test(expected = IllegalArgumentException::class) fun rejectsUnrelatedEndpoint() { ControllerSensors.parse("""{"ok":true}""") }
    @Test fun derivesHttpPortIndependentlyOfWebSocketPort() {
        assertEquals("http://192.168.4.1/api/status", ControllerTelemetryClient.endpoint("ws://192.168.4.1:81").toString())
    }
    @Test fun lostHttpTelemetryClearsHealthyReadings() = runTest {
        var requests = 0
        val factory = Call.Factory { request ->
            object : Call {
                private var cancelled = false
                override fun request() = request
                override fun execute(): Response {
                    requests++; if (requests > 1) throw IOException("offline")
                    return Response.Builder().request(request).protocol(Protocol.HTTP_1_1).code(200).message("OK")
                        .body("""{"uptime_ms":100,"state":"MONITOR","imu":true}""".toResponseBody()).build()
                }
                override fun enqueue(callback: Callback) = error("Not used")
                override fun cancel() { cancelled = true }
                override fun isExecuted() = requests > 0
                override fun isCanceled() = cancelled
                override fun clone(): Call = this
                override fun timeout() = Timeout.NONE
            }
        }
        val client = ControllerTelemetryClient(backgroundScope, factory)
        client.start("ws://192.168.4.1:81"); runCurrent()
        assertEquals("true", client.telemetry.value.sensors?.values?.get("IMU healthy"))
        advanceTimeBy(1000); runCurrent(); assertNull(client.telemetry.value.sensors)
        client.stop(); assertEquals("Disconnected", client.telemetry.value.message)
    }
}

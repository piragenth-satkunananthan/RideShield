package com.rideshield.phone.network

import com.rideshield.phone.protocol.*
import kotlinx.coroutines.test.*
import okhttp3.*
import okio.ByteString
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.Executors

class ControllerConnectionTest {
    private class Socket(private val req: Request, val listener: WebSocketListener) : WebSocket {
        val packets = mutableListOf<RswPacket>()
        override fun request() = req
        override fun queueSize() = 0L
        override fun send(text: String) = false
        override fun send(bytes: ByteString): Boolean { packets += requireNotNull(RswCodec.decode(bytes.toByteArray())); return true }
        override fun close(code: Int, reason: String?) = true
        override fun cancel() = Unit
        fun receive(text: String) = listener.onMessage(this, text)
    }
    private class Factory : WebSocket.Factory {
        val sockets = mutableListOf<Socket>()
        override fun newWebSocket(request: Request, listener: WebSocketListener): WebSocket =
            Socket(request, listener).also { sockets += it }
    }
    private fun handshake(s: Socket) {
        s.receive("""{"type":"welcome","version":1,"session":42}""")
        s.receive("""{"type":"sync","id":1,"controller_ms":5000}""")
        s.receive("""{"type":"status","profile":"HARDWARE","state":"MONITOR","send":"NONE","warning":"NONE","front_valid":true,"rear_valid":false}""")
    }
    @Test fun authenticationRequiresControllerResponseAndDisconnectClearsReadings() = runTest {
        val f = Factory(); val client = RswWebSocketClient({1000 + testScheduler.currentTime}, backgroundScope, f)
        client.connect(); val s = f.sockets.last()
        s.receive("""{"type":"welcome","version":1,"session":42}""")
        assertEquals(1, s.packets.size); assertEquals(1L, s.packets[0].sequence)
        assertFalse(client.telemetry.value.isClockSynced)
        assertFalse(client.sendFrontObservation(1000, 0, true, false, 80))
        s.receive("""{"type":"sync","id":1,"controller_ms":5000}""")
        s.receive("""{"type":"status","profile":"HARDWARE","state":"MONITOR","send":"NONE","warning":"NONE","front_valid":true,"rear_valid":false}""")
        assertEquals(ConnectionStatus.CONNECTED_ACTIVE, client.telemetry.value.status)
        client.disconnect()
        assertFalse(client.telemetry.value.controllerFrontValid); assertEquals("OFFLINE", client.telemetry.value.controllerState)
        s.listener.onFailure(s, Exception("late callback"), null)
        assertEquals(ConnectionStatus.DISCONNECTED, client.telemetry.value.status)
    }
    @Test fun oldCallbacksCannotResetNewSession() = runTest {
        val f = Factory(); val client = RswWebSocketClient({1000}, backgroundScope, f)
        client.connect(); val old = f.sockets.last()
        client.connect(); val fresh = f.sockets.last(); handshake(fresh)
        old.listener.onClosed(old, 1000, "old close")
        old.receive("""{"type":"welcome","version":1,"session":999}""")
        assertEquals(42L, client.telemetry.value.sessionId)
        assertEquals(ConnectionStatus.CONNECTED_ACTIVE, client.telemetry.value.status)
        assertEquals(1L, fresh.packets.first().sequence); client.disconnect()
    }
    @Test fun silentControllerExpiresAndReconnectsWithoutStaleState() = runTest {
        val f = Factory(); val client = RswWebSocketClient({1000 + testScheduler.currentTime}, backgroundScope, f)
        client.connect(); handshake(f.sockets.last()); runCurrent()
        advanceTimeBy(3500); runCurrent()
        assertEquals(ConnectionStatus.RECONNECTING, client.telemetry.value.status)
        assertEquals("OFFLINE", client.telemetry.value.controllerState)
        assertFalse(client.telemetry.value.controllerFrontValid)
        advanceTimeBy(1500); runCurrent(); assertEquals(2, f.sockets.size)
        handshake(f.sockets.last()); assertEquals(1L, f.sockets.last().packets.first().sequence); client.disconnect()
    }
    @Test fun simultaneousProducersKeepWireSequenceOrdered() = runTest {
        val f = Factory(); val client = RswWebSocketClient({1000}, backgroundScope, f)
        client.connect(); val s = f.sockets.last(); handshake(s)
        val workers = Executors.newFixedThreadPool(4)
        try {
            (1..20).map { n -> workers.submit<Boolean> {
                if (n % 2 == 0) client.sendFrontObservation(900L+n, 10, true, false, 90)
                else client.sendRearObservation(900L+n, 10, true, 90)
            } }.forEach { it.get() }
            assertTrue(s.packets.size > 2)
            assertTrue(s.packets.zipWithNext().all { (a,b) -> a.sequence < b.sequence })
        } finally { workers.shutdownNow(); client.disconnect() }
    }
    @Test fun invalidSettingsNeverOpenASocket() {
        val f = Factory(); val client = RswWebSocketClient({1000}, socketFactory = f)
        client.connect("not a URL", RswConstants.DEFAULT_TOKEN)
        client.connect(RswConstants.DEFAULT_CONTROLLER_URL, "é".repeat(32))
        assertTrue(f.sockets.isEmpty()); assertEquals(ConnectionStatus.ERROR, client.telemetry.value.status)
    }
}

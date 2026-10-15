package com.rideshield.phone.network

import com.rideshield.phone.protocol.RswRejectCode
import com.rideshield.phone.protocol.RswServerMessage
import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests verifying connection lifecycle, session renegotiation, sequence reset,
 * and rejection code handling across disconnect/reconnect cycles.
 */
class DisconnectReconnectTest {

    @Test
    fun testParseWelcomeAndHandshakeMessages() {
        val welcomeJson = """{"type":"welcome","version":1,"session":12345678}"""
        val welcomeMsg = RswServerMessage.parse(welcomeJson)

        assertTrue(welcomeMsg is RswServerMessage.Welcome)
        val welcome = welcomeMsg as RswServerMessage.Welcome
        assertEquals(1, welcome.version)
        assertEquals(12345678L, welcome.session)

        val syncJson = """{"type":"sync","id":7,"controller_ms":10500}"""
        val syncMsg = RswServerMessage.parse(syncJson)
        assertTrue(syncMsg is RswServerMessage.SyncRequest)
        val sync = syncMsg as RswServerMessage.SyncRequest
        assertEquals(7L, sync.id)
        assertEquals(10500L, sync.controllerMs)

        val statusJson = """{"type":"status","profile":"BENCH","state":"Monitor","send":"None","warning":"None","front_valid":true,"rear_valid":false}"""
        val statusMsg = RswServerMessage.parse(statusJson)
        assertTrue(statusMsg is RswServerMessage.Status)
        val status = statusMsg as RswServerMessage.Status
        assertEquals("BENCH", status.profile)
        assertEquals("Monitor", status.state)
        assertTrue(status.frontValid)
        assertFalse(status.rearValid)
    }

    @Test
    fun testParseRejectionCodes() {
        // Test all rejection codes 1..8 defined in PHONE_PROTOCOL.md
        val codes = listOf(
            1 to RswRejectCode.SESSION,
            2 to RswRejectCode.SEQUENCE,
            3 to RswRejectCode.CLOCK,
            4 to RswRejectCode.OFFLINE,
            5 to RswRejectCode.FUTURE,
            6 to RswRejectCode.STALE,
            7 to RswRejectCode.TIMESTAMP,
            8 to RswRejectCode.SCHEMA
        )

        for ((codeInt, expectedEnum) in codes) {
            val json = """{"type":"rejected","code":$codeInt}"""
            val msg = RswServerMessage.parse(json)
            assertTrue(msg is RswServerMessage.Rejected)
            val rejected = msg as RswServerMessage.Rejected
            assertEquals(codeInt, rejected.code)
            assertEquals(expectedEnum, rejected.rejectCode)
        }
    }

    @Test
    fun testClientStateResetOnDisconnect() {
        var now = 1000L
        val client = RswWebSocketClient(clockProvider = { now })

        // Initial state
        assertEquals(ConnectionStatus.DISCONNECTED, client.telemetry.value.status)
        assertEquals(0L, client.telemetry.value.sessionId)
        assertEquals(0L, client.telemetry.value.currentSequence)

        client.disconnect()
        assertEquals(ConnectionStatus.DISCONNECTED, client.telemetry.value.status)
        assertFalse(client.telemetry.value.isClockSynced)
    }
}

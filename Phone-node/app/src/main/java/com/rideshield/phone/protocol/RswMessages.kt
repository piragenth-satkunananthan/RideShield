package com.rideshield.phone.protocol

import org.json.JSONObject

/**
 * Incoming JSON messages from ESP32-S3 main controller over WebSocket.
 */
sealed class RswServerMessage {

    data class Welcome(
        val version: Int,
        val session: Long
    ) : RswServerMessage()

    data class SyncRequest(
        val id: Long,
        val controllerMs: Long
    ) : RswServerMessage()

    data class Status(
        val profile: String,
        val state: String,
        val send: String,
        val warning: String,
        val frontValid: Boolean,
        val rearValid: Boolean
    ) : RswServerMessage()

    data class Rejected(
        val code: Int,
        val rejectCode: RswRejectCode,
        val description: String
    ) : RswServerMessage()

    data class Unknown(
        val rawJson: String
    ) : RswServerMessage()

    companion object {
        fun parse(text: String): RswServerMessage {
            return try {
                val json = JSONObject(text)
                when (json.optString("type")) {
                    "welcome" -> Welcome(
                        version = json.optInt("version", 1),
                        session = json.optLong("session")
                    )
                    "sync" -> SyncRequest(
                        id = json.optLong("id"),
                        controllerMs = json.optLong("controller_ms")
                    )
                    "status" -> Status(
                        profile = json.optString("profile", "UNKNOWN"),
                        state = json.optString("state", "UNKNOWN"),
                        send = json.optString("send", "NONE"),
                        warning = json.optString("warning", "NONE"),
                        frontValid = json.optBoolean("front_valid", false),
                        rearValid = json.optBoolean("rear_valid", false)
                    )
                    "rejected" -> {
                        val code = json.optInt("code", 0)
                        val reject = RswRejectCode.fromCode(code)
                        Rejected(code, reject, reject.description)
                    }
                    else -> Unknown(text)
                }
            } catch (e: Exception) {
                Unknown(text)
            }
        }
    }
}

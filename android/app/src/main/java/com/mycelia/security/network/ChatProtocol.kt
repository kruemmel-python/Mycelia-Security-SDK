package com.mycelia.security.network

import android.util.Base64
import org.json.JSONObject

sealed class ChatPayload(val type: String) {
    data class Join(val roomId: String) : ChatPayload("join")
    data class Message(val roomId: String, val bodyCipher: ByteArray, val counter: Long) : ChatPayload("message")

    fun toJson(): JSONObject {
        val json = JSONObject()
        json.put("roomId", when (this) {
            is Join -> roomId
            is Message -> roomId
        })
        json.put("type", type)
        if (this is Message) {
            json.put("bodyCipherBase64", Base64.encodeToString(bodyCipher, Base64.NO_WRAP))
            json.put("counter", counter)
        }
        return json
    }

    companion object {
        fun fromJson(json: JSONObject): ChatPayload? {
            val type = json.optString("type")
            val roomId = json.optString("roomId")
            return when (type) {
                "join" -> Join(roomId)
                "message" -> {
                    val cipherB64 = json.optString("bodyCipherBase64")
                    val counter = json.optLong("counter")
                    val bodyCipher = Base64.decode(cipherB64, Base64.NO_WRAP)
                    Message(roomId, bodyCipher, counter)
                }
                else -> null
            }
        }
    }
}

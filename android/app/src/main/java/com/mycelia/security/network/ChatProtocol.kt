package com.mycelia.security.network

import android.util.Base64
import org.json.JSONObject

sealed class ChatPayload(val type: String) {
    data class Join(val roomId: String) : ChatPayload("join")
    data class Hello(val roomId: String, val publicKey: ByteArray, val mac: ByteArray) : ChatPayload("hello")
    data class Message(val roomId: String, val bodyCipher: ByteArray, val counter: Long) : ChatPayload("message")

    fun toJson(): JSONObject {
        val json = JSONObject()
        json.put("roomId", when (this) {
            is Join -> roomId
            is Hello -> roomId
            is Message -> roomId
        })
        json.put("type", type)
        if (this is Hello) {
            json.put("publicKeyB64", Base64.encodeToString(publicKey, Base64.NO_WRAP))
            json.put("macB64", Base64.encodeToString(mac, Base64.NO_WRAP))
        }
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
                "hello" -> {
                    val pubB64 = json.optString("publicKeyB64")
                    val macB64 = json.optString("macB64")
                    val pub = Base64.decode(pubB64, Base64.NO_WRAP)
                    val mac = Base64.decode(macB64, Base64.NO_WRAP)
                    Hello(roomId, pub, mac)
                }
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

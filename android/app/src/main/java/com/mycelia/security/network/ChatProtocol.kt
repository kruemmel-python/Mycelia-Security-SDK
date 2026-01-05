package com.mycelia.security.network

import android.util.Base64
import org.json.JSONObject

sealed class ChatPayload(val type: String) {
    data class Join(val roomId: String) : ChatPayload("join")
    data class Hello(val roomId: String, val publicKey: ByteArray, val mac: ByteArray) : ChatPayload("hello")
    data class FileChunk(
        val roomId: String,
        val fileId: String,
        val fileName: String,
        val mimeType: String,
        val fileSize: Long,
        val index: Int,
        val totalChunks: Int,
        val bodyCipher: ByteArray,
        val counter: Long
    ) : ChatPayload("file_chunk")
    data class Message(val roomId: String, val bodyCipher: ByteArray, val counter: Long) : ChatPayload("message")

    fun toJson(): JSONObject {
        val json = JSONObject()
        json.put("roomId", when (this) {
            is Join -> roomId
            is Hello -> roomId
            is FileChunk -> roomId
            is Message -> roomId
        })
        json.put("type", type)
        if (this is Hello) {
            json.put("publicKeyB64", Base64.encodeToString(publicKey, Base64.NO_WRAP))
            json.put("macB64", Base64.encodeToString(mac, Base64.NO_WRAP))
        }
        if (this is FileChunk) {
            json.put("fileId", fileId)
            json.put("fileName", fileName)
            json.put("mimeType", mimeType)
            json.put("fileSize", fileSize)
            json.put("index", index)
            json.put("totalChunks", totalChunks)
            json.put("bodyCipherBase64", Base64.encodeToString(bodyCipher, Base64.NO_WRAP))
            json.put("counter", counter)
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
                "file_chunk" -> {
                    val fileId = json.optString("fileId")
                    val fileName = json.optString("fileName")
                    val mimeType = json.optString("mimeType")
                    val fileSize = json.optLong("fileSize")
                    val index = json.optInt("index")
                    val totalChunks = json.optInt("totalChunks")
                    val cipherB64 = json.optString("bodyCipherBase64")
                    val counter = json.optLong("counter")
                    val bodyCipher = Base64.decode(cipherB64, Base64.NO_WRAP)
                    FileChunk(roomId, fileId, fileName, mimeType, fileSize, index, totalChunks, bodyCipher, counter)
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

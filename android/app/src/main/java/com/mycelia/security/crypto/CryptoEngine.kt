package com.mycelia.security.crypto

import android.content.Context
import java.io.File
import java.io.FileOutputStream

class CryptoEngine(private val context: Context) {
    private val native: MyceliaNative?
    private val handle: Long
    val nativeAvailable: Boolean

    init {
        var nativeHandle: Long
        var nativeImpl: MyceliaNative?
        val shaderDir = runCatching { ShaderAssetManager.ensureShaders(context) }.getOrNull()
        if (shaderDir == null) {
            nativeImpl = null
            nativeHandle = 0L
        } else {
            val initResult = runCatching {
                val impl = MyceliaNative()
                val handle = impl.nativeInit(shaderDir)
                impl to handle
            }.getOrNull()
            if (initResult == null || initResult.second == 0L) {
                nativeImpl = null
                nativeHandle = 0L
            } else {
                nativeImpl = initResult.first
                nativeHandle = initResult.second
            }
        }
        native = nativeImpl
        handle = nativeHandle
        nativeAvailable = handle != 0L
    }

    fun encrypt(input: ByteArray, seed: ByteArray, streamOffset: Long): ByteArray {
        val impl = native ?: error("Native crypto not available")
        return impl.nativeEncrypt(handle, input, seed, streamOffset)
            ?: error("Native encrypt returned null")
    }

    fun decrypt(input: ByteArray, seed: ByteArray, streamOffset: Long): ByteArray {
        val impl = native ?: error("Native crypto not available")
        return impl.nativeDecrypt(handle, input, seed, streamOffset)
            ?: error("Native decrypt returned null")
    }

    fun aeadEncrypt(key: ByteArray, nonce: ByteArray, plaintext: ByteArray, aad: ByteArray): ByteArray {
        return AeadCipher.encrypt(key, nonce, plaintext, aad)
    }

    fun aeadDecrypt(key: ByteArray, nonce: ByteArray, ciphertext: ByteArray, aad: ByteArray): ByteArray {
        return AeadCipher.decrypt(key, nonce, ciphertext, aad)
    }

    fun shutdown() {
        native?.nativeRelease(handle)
    }
}

object ShaderAssetManager {
    private const val SHADER_FILE = "mycelia_keystream_xor.spv"

    fun ensureShaders(context: Context): String {
        val dir = File(context.filesDir, "shaders")
        if (!dir.exists()) {
            dir.mkdirs()
        }
        val outputFile = File(dir, SHADER_FILE)
        if (!outputFile.exists()) {
            context.assets.open("shaders/$SHADER_FILE").use { input ->
                FileOutputStream(outputFile).use { output ->
                    input.copyTo(output)
                }
            }
        }
        return dir.absolutePath
    }
}

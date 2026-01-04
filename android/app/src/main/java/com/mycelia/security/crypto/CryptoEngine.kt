package com.mycelia.security.crypto

import android.content.Context
import java.io.File
import java.io.FileOutputStream

class CryptoEngine(private val context: Context) {
    private val native = MyceliaNative()
    private val handle: Long

    init {
        val shaderDir = ShaderAssetManager.ensureShaders(context)
        handle = native.nativeInit(shaderDir)
        require(handle != 0L) { "Failed to initialize native crypto engine" }
    }

    fun encrypt(input: ByteArray, seed: ByteArray, streamOffset: Long): ByteArray {
        return native.nativeEncrypt(handle, input, seed, streamOffset)
            ?: error("Native encrypt returned null")
    }

    fun decrypt(input: ByteArray, seed: ByteArray, streamOffset: Long): ByteArray {
        return native.nativeDecrypt(handle, input, seed, streamOffset)
            ?: error("Native decrypt returned null")
    }

    fun shutdown() {
        native.nativeRelease(handle)
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

package com.mycelia.security.crypto

class MyceliaNative {
    external fun nativeInit(shaderDir: String): Long
    external fun nativeRelease(handle: Long)
    external fun nativeEncrypt(handle: Long, input: ByteArray, seed: ByteArray, streamOffset: Long): ByteArray?
    external fun nativeDecrypt(handle: Long, input: ByteArray, seed: ByteArray, streamOffset: Long): ByteArray?

    companion object {
        init {
            System.loadLibrary("mycelia_native")
        }
    }
}

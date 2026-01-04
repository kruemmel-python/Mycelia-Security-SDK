package com.mycelia.security.crypto

import android.util.Base64
import java.security.SecureRandom

object SeedManager {
    private val random = SecureRandom()
    private val subqgRng = SubqgRng()

    fun generateSeed(size: Int = 32): ByteArray {
        val output = ByteArray(size)
        val strong = ByteArray(size).also { random.nextBytes(it) }
        val subqg = subqgRng.generateEntropy(size)
        for (i in output.indices) {
            output[i] = (strong[i].toInt() xor subqg[i].toInt()).toByte()
        }
        return output
    }

    fun encodeSeed(seed: ByteArray): String {
        return Base64.encodeToString(seed, Base64.NO_WRAP)
    }

    fun decodeSeed(seedB64: String): ByteArray {
        return Base64.decode(seedB64, Base64.NO_WRAP)
    }
}

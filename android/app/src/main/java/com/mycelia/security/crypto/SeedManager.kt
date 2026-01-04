package com.mycelia.security.crypto

import android.util.Base64
import java.security.SecureRandom

object SeedManager {
    private val random = SecureRandom()

    fun generateSeed(size: Int = 32): ByteArray {
        return ByteArray(size).also { random.nextBytes(it) }
    }

    fun encodeSeed(seed: ByteArray): String {
        return Base64.encodeToString(seed, Base64.NO_WRAP)
    }

    fun decodeSeed(seedB64: String): ByteArray {
        return Base64.decode(seedB64, Base64.NO_WRAP)
    }
}

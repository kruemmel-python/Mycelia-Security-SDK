package com.mycelia.security.crypto

import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import kotlin.math.ceil

object Hkdf {
    private const val HMAC_ALGORITHM = "HmacSHA256"

    fun deriveKey(ikm: ByteArray, salt: ByteArray, info: ByteArray, length: Int): ByteArray {
        val prk = extract(salt, ikm)
        return expand(prk, info, length)
    }

    private fun extract(salt: ByteArray, ikm: ByteArray): ByteArray {
        val mac = Mac.getInstance(HMAC_ALGORITHM)
        mac.init(SecretKeySpec(salt, HMAC_ALGORITHM))
        return mac.doFinal(ikm)
    }

    private fun expand(prk: ByteArray, info: ByteArray, length: Int): ByteArray {
        val mac = Mac.getInstance(HMAC_ALGORITHM)
        mac.init(SecretKeySpec(prk, HMAC_ALGORITHM))
        val hashLen = mac.macLength
        val n = ceil(length.toDouble() / hashLen.toDouble()).toInt()
        var t = ByteArray(0)
        val okm = ByteArray(length)
        var offset = 0
        for (i in 1..n) {
            mac.reset()
            mac.update(t)
            mac.update(info)
            mac.update(i.toByte())
            t = mac.doFinal()
            val toCopy = minOf(hashLen, length - offset)
            System.arraycopy(t, 0, okm, offset, toCopy)
            offset += toCopy
        }
        return okm
    }
}

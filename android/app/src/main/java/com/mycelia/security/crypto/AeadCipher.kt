package com.mycelia.security.crypto

import org.bouncycastle.crypto.InvalidCipherTextException
import org.bouncycastle.crypto.modes.ChaCha20Poly1305
import org.bouncycastle.crypto.params.AEADParameters
import org.bouncycastle.crypto.params.KeyParameter

object AeadCipher {
    private const val MAC_SIZE_BITS = 128

    fun encrypt(key: ByteArray, nonce: ByteArray, plaintext: ByteArray, aad: ByteArray): ByteArray {
        val cipher = ChaCha20Poly1305()
        val params = AEADParameters(KeyParameter(key), MAC_SIZE_BITS, nonce, aad)
        cipher.init(true, params)
        val output = ByteArray(cipher.getOutputSize(plaintext.size))
        var outLen = cipher.processBytes(plaintext, 0, plaintext.size, output, 0)
        outLen += cipher.doFinal(output, outLen)
        return output.copyOf(outLen)
    }

    fun decrypt(key: ByteArray, nonce: ByteArray, ciphertext: ByteArray, aad: ByteArray): ByteArray {
        val cipher = ChaCha20Poly1305()
        val params = AEADParameters(KeyParameter(key), MAC_SIZE_BITS, nonce, aad)
        cipher.init(false, params)
        val output = ByteArray(cipher.getOutputSize(ciphertext.size))
        var outLen = cipher.processBytes(ciphertext, 0, ciphertext.size, output, 0)
        try {
            outLen += cipher.doFinal(output, outLen)
        } catch (ex: InvalidCipherTextException) {
            throw IllegalStateException("AEAD authentication failed", ex)
        }
        return output.copyOf(outLen)
    }
}

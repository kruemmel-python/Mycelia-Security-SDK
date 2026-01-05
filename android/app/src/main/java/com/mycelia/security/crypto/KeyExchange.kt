package com.mycelia.security.crypto

import android.util.Base64
import org.bouncycastle.math.ec.rfc7748.X25519
import java.security.SecureRandom
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

data class X25519KeyPair(val publicKey: ByteArray, val privateKey: ByteArray)

object KeyExchange {
    private val random = SecureRandom()
    private const val MAC_ALGORITHM = "HmacSHA256"

    fun generateKeyPair(): X25519KeyPair {
        val privateKey = ByteArray(X25519.SCALAR_SIZE)
        val publicKey = ByteArray(X25519.POINT_SIZE)
        X25519.generatePrivateKey(random, privateKey)
        X25519.generatePublicKey(privateKey, 0, publicKey, 0)
        return X25519KeyPair(publicKey, privateKey)
    }

    fun deriveSharedSecret(privateKey: ByteArray, peerPublicKey: ByteArray): ByteArray {
        val out = ByteArray(X25519.POINT_SIZE)
        X25519.scalarMult(privateKey, 0, peerPublicKey, 0, out, 0)
        return out
    }

    fun computeHelloMac(seed: ByteArray, publicKey: ByteArray): ByteArray {
        val mac = Mac.getInstance(MAC_ALGORITHM)
        mac.init(SecretKeySpec(seed, MAC_ALGORITHM))
        return mac.doFinal(publicKey)
    }

    fun encodeKey(key: ByteArray): String = Base64.encodeToString(key, Base64.NO_WRAP)

    fun decodeKey(encoded: String): ByteArray = Base64.decode(encoded, Base64.NO_WRAP)
}

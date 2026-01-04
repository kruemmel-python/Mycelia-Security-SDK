package com.mycelia.security.network

import java.nio.ByteBuffer
import java.nio.ByteOrder

object Framing {
    const val MAX_FRAME_SIZE = 10 * 1024 * 1024

    fun encode(payload: ByteArray): ByteArray {
        require(payload.size <= MAX_FRAME_SIZE) { "Frame too large" }
        val buffer = ByteBuffer.allocate(4 + payload.size).order(ByteOrder.BIG_ENDIAN)
        buffer.putInt(payload.size)
        buffer.put(payload)
        return buffer.array()
    }

    fun decodeLength(prefix: ByteArray): Int {
        val buffer = ByteBuffer.wrap(prefix).order(ByteOrder.BIG_ENDIAN)
        return buffer.int
    }
}

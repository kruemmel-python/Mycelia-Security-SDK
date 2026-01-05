package com.mycelia.security.crypto

object CounterManager {
    const val STRIDE_BYTES = 4096L

    fun streamOffset(counter: Long, byteOffset: Long = 0): Long {
        require(counter >= 0) { "Counter must be non-negative" }
        require(byteOffset >= 0) { "Byte offset must be non-negative" }
        return counter * STRIDE_BYTES + byteOffset
    }
}

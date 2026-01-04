package com.mycelia.security

import com.mycelia.security.network.Framing
import org.junit.Assert.assertEquals
import org.junit.Test

class FramingTest {
    @Test
    fun encodeAddsLengthPrefix() {
        val payload = "hello".toByteArray()
        val frame = Framing.encode(payload)
        val length = Framing.decodeLength(frame.copyOfRange(0, 4))
        assertEquals(payload.size, length)
    }
}

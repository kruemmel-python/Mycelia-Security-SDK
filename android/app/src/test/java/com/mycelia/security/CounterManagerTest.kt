package com.mycelia.security

import com.mycelia.security.crypto.CounterManager
import org.junit.Assert.assertEquals
import org.junit.Test

class CounterManagerTest {
    @Test
    fun streamOffsetUsesStride() {
        val offset = CounterManager.streamOffset(2, 128)
        assertEquals(2 * CounterManager.STRIDE_BYTES + 128, offset)
    }
}

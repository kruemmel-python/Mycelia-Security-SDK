package com.mycelia.security

import com.mycelia.security.crypto.SeedManager
import org.junit.Assert.assertArrayEquals
import org.junit.Test

class SeedManagerTest {
    @Test
    fun encodeDecodeRoundTrip() {
        val seed = SeedManager.generateSeed(32)
        val encoded = SeedManager.encodeSeed(seed)
        val decoded = SeedManager.decodeSeed(encoded)
        assertArrayEquals(seed, decoded)
    }
}

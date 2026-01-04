package com.mycelia.security.crypto

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.SecureRandom
import kotlin.math.abs
import kotlin.math.asin
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

class SubqgRng(
    private val width: Int = 32,
    private val height: Int = 32,
    private val iterations: Int = 6
) {
    private val random = SecureRandom()
    private val cellCount = width * height

    fun generateEntropy(bytes: Int): ByteArray {
        val energy = FloatArray(cellCount)
        val pressure = FloatArray(cellCount)
        val gravity = FloatArray(cellCount)
        val magnetism = FloatArray(cellCount)
        val temperature = FloatArray(cellCount)
        val potential = FloatArray(cellCount)
        val driftX = FloatArray(cellCount)
        val driftY = FloatArray(cellCount)
        val phase = FloatArray(cellCount)

        val rngEnergy = FloatArray(cellCount) { random.nextFloat() }
        val rngPhase = FloatArray(cellCount) { random.nextFloat() }
        val rngSpin = FloatArray(cellCount) { random.nextFloat() }

        for (i in 0 until cellCount) {
            energy[i] = 0.5f
            phase[i] = 0.25f
        }

        repeat(iterations) {
            for (idx in 0 until cellCount) {
                val x = idx % width
                val y = idx / width
                val lapE = laplace(energy, x, y)
                val lapP = laplace(pressure, x, y)
                val lapG = laplace(gravity, x, y)
                val lapM = laplace(magnetism, x, y)
                val lapT = laplace(temperature, x, y)
                val lapV = laplace(potential, x, y)

                val noise = (rngEnergy[idx] - 0.5f) * 0.3f
                val noiseP = (rngPhase[idx] - 0.5f) * 0.3f
                val noiseM = (rngSpin[idx] - 0.5f) * 0.3f

                var e = energy[idx] + 0.10f * lapE + noise
                var p = pressure[idx] + 0.08f * lapP + 0.05f * (energy[idx] - pressure[idx]) + noiseP
                var t = temperature[idx] + 0.05f * lapT + 0.10f * (energy[idx] - temperature[idx])
                var v = potential[idx] + 0.04f * lapV + 0.04f * (pressure[idx] + gravity[idx] - 2.0f * potential[idx])
                var g = gravity[idx] + 0.02f * lapG + 0.08f * (potential[idx] - gravity[idx])
                var m = magnetism[idx] + 0.03f * lapM + 0.02f * (abs(driftX[idx]) + abs(driftY[idx])) + noiseM

                e = clampField(e)
                p = clampField(p)
                t = clampField(t)
                v = clampField(v)
                g = clampField(g)
                m = clampField(m)

                val gradEx = 0.5f * (sample(energy, x + 1, y) - sample(energy, x - 1, y))
                val gradEy = 0.5f * (sample(energy, x, y + 1) - sample(energy, x, y - 1))
                driftX[idx] = 0.95f * driftX[idx] + 0.05f * gradEx
                driftY[idx] = 0.95f * driftY[idx] + 0.05f * gradEy

                val currentPhase = clampField(phase[idx])
                val phaseAcc = asin(currentPhase.toDouble()) / Math.PI + (noiseP * 0.2f)
                phase[idx] = sin(phaseAcc * Math.PI).toFloat()

                energy[idx] = e
                pressure[idx] = p
                temperature[idx] = t
                potential[idx] = v
                gravity[idx] = g
                magnetism[idx] = m
            }
        }

        val buffer = ByteBuffer.allocate(cellCount * 4).order(ByteOrder.LITTLE_ENDIAN)
        for (i in 0 until cellCount) {
            val mix = energy[i] * 0.4f + pressure[i] * 0.2f + temperature[i] * 0.2f + potential[i] * 0.2f
            buffer.putInt(java.lang.Float.floatToIntBits(mix))
        }
        val data = buffer.array()
        val out = ByteArray(bytes)
        var offset = 0
        while (offset < bytes) {
            val slice = minOf(bytes - offset, data.size)
            System.arraycopy(data, 0, out, offset, slice)
            offset += slice
        }
        return out
    }

    private fun clampField(v: Float, lo: Float = -1.0f, hi: Float = 1.0f): Float {
        return min(hi, max(lo, v))
    }

    private fun clampIndex(x: Int, y: Int): Int {
        val cx = x.coerceIn(0, width - 1)
        val cy = y.coerceIn(0, height - 1)
        return cy * width + cx
    }

    private fun sample(field: FloatArray, x: Int, y: Int): Float {
        return field[clampIndex(x, y)]
    }

    private fun laplace(field: FloatArray, x: Int, y: Int): Float {
        return sample(field, x, y) +
            sample(field, x - 1, y) +
            sample(field, x + 1, y) +
            sample(field, x, y - 1) +
            sample(field, x, y + 1) -
            4.0f * sample(field, x, y)
    }
}

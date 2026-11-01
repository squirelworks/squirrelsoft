package works.squirrel.sound

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

class FftTest {
    @Test
    fun detectsDemoToneBin() {
        val n = 1024
        val sr = 44100.0
        val f0 = 440.0
        val re = FloatArray(n)
        val im = FloatArray(n)
        val w = Fft.hann(n)
        for (i in 0 until n) {
            re[i] = (sin(2.0 * PI * f0 * i / sr) * w[i]).toFloat()
        }
        Fft.forward(re, im)
        val mags = FloatArray(n / 2)
        Fft.magnitudeDb(re, im, mags)
        var peak = 0
        var best = -999f
        for (i in 1 until mags.size) {
            if (mags[i] > best) {
                best = mags[i]
                peak = i
            }
        }
        val hz = peak * sr / n
        assertTrue("peak near 440, got $hz", abs(hz - 440.0) < 30.0)
        assertEquals(n, w.size)
        assertTrue("peak should be well above floor, got $best", best > -40f)
    }

    @Test
    fun silenceStaysNearFloor() {
        val n = 512
        val re = FloatArray(n)
        val im = FloatArray(n)
        Fft.forward(re, im)
        val mags = FloatArray(n / 2)
        Fft.magnitudeDb(re, im, mags)
        for (v in mags) {
            assertEquals(-90f, v, 0.01f)
        }
    }
}

class RingBufferTest {
    @Test
    fun hopKeepsOverlapAndDropsBacklog() {
        val rb = RingBuffer(64)
        val chunk = FloatArray(32) { it.toFloat() }
        rb.write(chunk, 0, 32)
        assertEquals(32, rb.available)

        val out = FloatArray(16)
        assertTrue(rb.takeWindow(out, fftSize = 16, hop = 4))
        // Most recent 16 samples of the 32 written: values 16..31
        assertEquals(16f, out[0], 0.001f)
        assertEquals(31f, out[15], 0.001f)
        // Overlap retained: fftSize - hop = 12
        assertEquals(12, rb.available)

        // Not enough for another full window yet.
        assertFalse(rb.takeWindow(out, fftSize = 16, hop = 4))

        val more = FloatArray(4) { (100 + it).toFloat() }
        rb.write(more, 0, 4)
        assertTrue(rb.takeWindow(out, fftSize = 16, hop = 4))
        assertEquals(12, rb.available)
    }

    @Test
    fun wrapAroundPreservesNewestWindow() {
        val rb = RingBuffer(8)
        rb.write(floatArrayOf(0f, 1f, 2f, 3f, 4f, 5f), 0, 6)
        assertTrue(rb.takeWindow(FloatArray(4), 4, 2)) // size becomes 2
        rb.write(floatArrayOf(6f, 7f, 8f, 9f), 0, 4) // wraps
        val out = FloatArray(4)
        assertTrue(rb.takeWindow(out, 4, 2))
        // Most recent four samples written into the ring ending at 9.
        assertEquals(6f, out[0], 0.001f)
        assertEquals(7f, out[1], 0.001f)
        assertEquals(8f, out[2], 0.001f)
        assertEquals(9f, out[3], 0.001f)
    }
}

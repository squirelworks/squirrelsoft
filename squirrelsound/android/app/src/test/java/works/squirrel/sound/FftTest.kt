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

class SpectrumBoundsTest {
    /** Mirrors SpectrumView bin window math; empty/short frames must not throw. */
    private fun binRange(n: Int, sampleRate: Int, fMin: Float, fMax: Float): Pair<Int, Int>? {
        if (n < 2 || sampleRate <= 0) return null
        val ny = sampleRate / 2f
        if (ny <= 0f) return null
        val lo = maxOf(0f, fMin)
        val hi = minOf(ny, fMax).coerceAtLeast(lo + 1f)
        val b0 = ((lo / ny) * n).toInt().coerceIn(0, n - 2)
        val b1 = ((hi / ny) * n).toInt().coerceIn(b0 + 1, n)
        return b0 to b1
    }

    @Test
    fun shortFramesAreRejected() {
        assertEquals(null, binRange(0, 44100, 20f, 8000f))
        assertEquals(null, binRange(1, 44100, 20f, 8000f))
        assertEquals(null, binRange(16, 0, 20f, 8000f))
    }

    @Test
    fun normalFrameProducesValidBins() {
        val (b0, b1) = binRange(1024, 44100, 20f, 8000f)!!
        assertTrue(b0 >= 0)
        assertTrue(b1 > b0)
        assertTrue(b1 <= 1024)
    }
}

class SpectrumControlsTest {
    @Test
    fun humanDefaultIsPhoneFriendlyHearing() {
        val (lo, hi) = SpectrumControls.RangePreset.HUMAN.resolve(22_050f)
        assertEquals(20f, lo, 0.01f)
        assertEquals(16_000f, hi, 0.01f)
    }

    @Test
    fun musicKeepsBassInFrame() {
        val (lo, hi) = SpectrumControls.RangePreset.MUSIC.resolve(22_050f)
        assertEquals(20f, lo, 0.01f)
        assertTrue("music top should leave bass visible across most of the plot", hi <= 5_000f)
        assertTrue(hi > 1_000f)
    }

    @Test
    fun bassPresetIsLowEnd() {
        val (lo, hi) = SpectrumControls.RangePreset.BASS.resolve(24_000f)
        assertEquals(20f, lo, 0.01f)
        assertEquals(250f, hi, 0.01f)
    }

    @Test
    fun fullUsesNyquist() {
        val (lo, hi) = SpectrumControls.RangePreset.FULL.resolve(24_000f)
        assertEquals(20f, lo, 0.01f)
        assertEquals(24_000f, hi, 0.01f)
    }

    @Test
    fun fullClampsWhenNyquistBelowHumanTop() {
        val (lo, hi) = SpectrumControls.RangePreset.HUMAN.resolve(8_000f)
        assertEquals(20f, lo, 0.01f)
        assertEquals(8_000f, hi, 0.01f)
    }

    @Test
    fun zoomInNarrowsSpan() {
        val (lo, hi) = SpectrumControls.zoom(20f, 16_000f, 2f, 22_050f)
        assertTrue(hi - lo < 16_000f - 20f)
        assertTrue(hi - lo >= SpectrumControls.MIN_SPAN_HZ)
    }

    @Test
    fun zoomOutWidensButStaysInNyquist() {
        val (lo, hi) = SpectrumControls.zoom(1_000f, 2_000f, 0.5f, 22_050f)
        assertTrue(hi - lo > 1_000f)
        assertTrue(hi <= 22_050f)
        assertTrue(lo >= 0f)
    }

    @Test
    fun panShiftsWindow() {
        val (lo, hi) = SpectrumControls.pan(100f, 1_100f, 0.5f, 22_050f)
        assertEquals(600f, lo, 0.5f)
        assertEquals(1_600f, hi, 0.5f)
    }

    @Test
    fun scrollSpeedMapsToHopMs() {
        assertEquals(50, SpectrumControls.hopMsForScroll(SpectrumControls.ScrollSpeed.SLOW))
        assertEquals(20, SpectrumControls.hopMsForScroll(SpectrumControls.ScrollSpeed.NORMAL))
        assertEquals(10, SpectrumControls.hopMsForScroll(SpectrumControls.ScrollSpeed.FAST))
        assertEquals(
            SpectrumControls.ScrollSpeed.SLOW,
            SpectrumControls.ScrollSpeed.fromHopMs(50)
        )
        assertEquals(
            SpectrumControls.ScrollSpeed.FAST,
            SpectrumControls.ScrollSpeed.fromHopMs(10)
        )
        assertEquals(
            SpectrumControls.ScrollSpeed.NORMAL,
            SpectrumControls.ScrollSpeed.fromHopMs(20)
        )
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

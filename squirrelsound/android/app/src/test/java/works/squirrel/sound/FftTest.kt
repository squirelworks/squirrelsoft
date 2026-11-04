package works.squirrel.sound

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
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
    fun waterfallDurationPlansCoverTheSpan() {
        for (sr in intArrayOf(44_100, 48_000)) {
            for (fft in intArrayOf(1024, 2048, 4096)) {
                for (d in SpectrumControls.WaterfallDuration.values()) {
                    val plan = SpectrumControls.rowPlan(d.seconds, 160, sr, fft)
                    assertTrue(plan.hopSamples in (fft / 16)..fft)
                    assertTrue(plan.framesPerRow >= 1)
                    val span = plan.rowSeconds(sr) * 160
                    assertEquals("$d sr=$sr fft=$fft", d.seconds, span, d.seconds * 0.02f)
                }
            }
        }
    }

    @Test
    fun longerSpanScrollsSlower() {
        val spans = SpectrumControls.WaterfallDuration.values().map {
            SpectrumControls.rowPlan(it.seconds, 160, 48_000, 2048).rowSeconds(48_000)
        }
        for (i in 1 until spans.size) assertTrue(spans[i] > spans[i - 1])
    }

    @Test
    fun waterfallPutsNewestAtTopFountainAtBottom() {
        val wf = SpectrumControls.WaterfallDirection.WATERFALL
        val fo = SpectrumControls.WaterfallDirection.FOUNTAIN
        assertEquals(0f, wf.yFraction(0f), 0f)
        assertEquals(1f, wf.yFraction(1f), 0f)
        assertEquals(1f, fo.yFraction(0f), 0f)
        assertEquals(0f, fo.yFraction(1f), 0f)
        assertEquals(0.75f, fo.yFraction(0.25f), 1e-6f)
        assertEquals(1f, fo.yFraction(-2f), 0f)
    }

    @Test
    fun directionIdsRoundTripAndDefault() {
        for (d in SpectrumControls.WaterfallDirection.values()) {
            assertEquals(d, SpectrumControls.WaterfallDirection.fromId(d.id))
        }
        assertEquals(
            SpectrumControls.WaterfallDirection.WATERFALL,
            SpectrumControls.WaterfallDirection.fromId(null)
        )
        assertEquals(
            SpectrumControls.WaterfallDirection.WATERFALL,
            SpectrumControls.WaterfallDirection.fromId("sideways")
        )
    }

    @Test
    fun timeLabels() {
        assertEquals("now", SpectrumControls.timeLabel(0f))
        assertEquals("-15s", SpectrumControls.timeLabel(15f))
        assertEquals("-3.8s", SpectrumControls.timeLabel(3.75f))
        assertEquals("-2m", SpectrumControls.timeLabel(120f))
        assertEquals("-1.3m", SpectrumControls.timeLabel(75f))
    }

    @Test
    fun rowAccumulatorAveragesFrames() {
        val acc = WaterfallRowAccumulator()
        assertNull(acc.add(floatArrayOf(-10f, -20f), 3))
        assertNull(acc.add(floatArrayOf(-20f, -30f), 3))
        val row = acc.add(floatArrayOf(-30f, -40f), 3)!!
        assertEquals(-20f, row[0], 0.001f)
        assertEquals(-30f, row[1], 0.001f)
        // Starts fresh after each row.
        val one = acc.add(floatArrayOf(-5f, -6f), 1)!!
        assertEquals(-5f, one[0], 0.001f)
    }
}

class RingBufferTest {
    @Test
    fun takeWindowConsumesExactlyOneHop() {
        val rb = RingBuffer(64)
        rb.write(FloatArray(32) { it.toFloat() }, 0, 32)
        val out = FloatArray(16)
        assertTrue(rb.takeWindow(out, fftSize = 16, hop = 4))
        // Oldest unconsumed window first: 0..15, then 4 samples consumed.
        assertEquals(0f, out[0], 0.001f)
        assertEquals(15f, out[15], 0.001f)
        assertEquals(28, rb.available)
        assertTrue(rb.takeWindow(out, fftSize = 16, hop = 4))
        assertEquals(4f, out[0], 0.001f)
        assertEquals(19f, out[15], 0.001f)
        assertEquals(24, rb.available)
    }

    @Test
    fun backlogCapDropsOldestToStayLive() {
        val rb = RingBuffer(128)
        rb.write(FloatArray(100) { it.toFloat() }, 0, 100)
        val out = FloatArray(16)
        assertTrue(rb.takeWindow(out, fftSize = 16, hop = 4, maxBacklog = 16))
        // Kept only the newest 32 samples (68..99) before taking the window.
        assertEquals(68f, out[0], 0.001f)
        assertEquals(28, rb.available)
    }

    @Test
    fun wrapAroundReadsInOrder() {
        val rb = RingBuffer(8)
        rb.write(floatArrayOf(0f, 1f, 2f, 3f, 4f, 5f), 0, 6)
        assertTrue(rb.takeWindow(FloatArray(4), 4, 2)) // consumes 0,1
        rb.write(floatArrayOf(6f, 7f, 8f, 9f), 0, 4) // wraps; ring holds 2..9
        val out = FloatArray(4)
        assertTrue(rb.takeWindow(out, 4, 2))
        assertEquals(listOf(2f, 3f, 4f, 5f), out.toList())
    }

    /**
     * The bug in #29: every hop setting produced one frame per FFT-sized read.
     * Feed 10 s through hop-sized reads (as AudioEngine does) and through
     * fixed FFT-sized reads; frames must follow 10 s / hop either way.
     */
    @Test
    fun framesPerTenSecondsFollowHop() {
        val sr = 48_000
        val fft = 4096 // hop must not exceed the FFT size; 50 ms = 2400 samples
        val total = sr * 10
        for (hopMs in intArrayOf(10, 20, 50)) {
            val hop = sr * hopMs / 1000
            val expected = (total - fft) / hop + 1
            for (readSize in intArrayOf(hop, fft, 480)) {
                val rb = RingBuffer(fft * 8)
                val chunk = FloatArray(readSize)
                val out = FloatArray(fft)
                var fed = 0
                var frames = 0
                while (fed < total) {
                    val n = minOf(readSize, total - fed)
                    rb.write(chunk, 0, n)
                    fed += n
                    while (rb.available >= fft && rb.takeWindow(out, fft, hop)) frames++
                }
                assertEquals("hop=$hopMs ms read=$readSize", expected.toFloat(), frames.toFloat(), 1f)
            }
        }
    }
}

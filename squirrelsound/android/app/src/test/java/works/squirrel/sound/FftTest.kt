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
        Fft.magnitudeDb(re, im, mags, Fft.windowSum(w))
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
        Fft.magnitudeDb(re, im, mags, Fft.windowSum(Fft.hann(n)))
        for (v in mags) {
            assertEquals(Levels.FLOOR_DB, v, 0.01f)
        }
    }

    /** Peak dBFS of a sine of [amp] at a bin-centred frequency. */
    private fun sinePeakDb(amp: Double, n: Int, bin: Int): Float {
        val w = Fft.hann(n)
        val re = FloatArray(n) { i -> (amp * sin(2.0 * PI * bin * i / n) * w[i]).toFloat() }
        val im = FloatArray(n)
        Fft.forward(re, im)
        val mags = FloatArray(n / 2)
        Fft.magnitudeDb(re, im, mags, Fft.windowSum(w))
        return mags.maxOrNull()!!
    }

    @Test
    fun fullScaleSineReadsZeroDbfs() {
        for (n in intArrayOf(1024, 2048, 4096)) {
            assertEquals("n=$n", 0f, sinePeakDb(1.0, n, n / 8), 0.5f)
        }
    }

    @Test
    fun minus20DbSineReadsMinus20() {
        assertEquals(-20f, sinePeakDb(0.1, 2048, 200), 0.5f)
        assertEquals(-60f, sinePeakDb(0.001, 2048, 77), 0.5f)
    }

    @Test
    fun dcUsesSingleSidedScale() {
        val n = 1024
        val w = Fft.hann(n)
        val re = FloatArray(n) { i -> 0.5f * w[i] }
        val im = FloatArray(n)
        Fft.forward(re, im)
        val mags = FloatArray(n / 2)
        Fft.magnitudeDb(re, im, mags, Fft.windowSum(w))
        assertEquals(20f * kotlin.math.log10(0.5f), mags[0], 0.05f)
    }
}

class LevelsTest {
    private fun rgb(c: Int) = Triple((c shr 16) and 0xFF, (c shr 8) and 0xFF, c and 0xFF)

    @Test
    fun colourMapIsBlackAtFloorAndWhiteAtTop() {
        assertEquals(Triple(0, 0, 0), rgb(Levels.colorAt(0f)))
        assertEquals(Triple(255, 255, 255), rgb(Levels.colorAt(1f)))
        assertEquals(0xFF, (Levels.colorAt(0.5f) ushr 24))
        // Out of range and NaN clamp instead of wrapping.
        assertEquals(Levels.colorAt(0f), Levels.colorAt(-3f))
        assertEquals(Levels.colorAt(1f), Levels.colorAt(7f))
        assertEquals(Levels.colorAt(0f), Levels.colorAt(Float.NaN))
    }

    @Test
    fun colourMapPassesThroughWarmColours() {
        val (r1, g1, b1) = rgb(Levels.colorAt(4f / 6f)) // yellow stop
        assertTrue(r1 > 200 && g1 > 200 && b1 < 50)
        val (r2, g2, _) = rgb(Levels.colorAt(5f / 6f)) // red stop
        assertTrue(r2 > 200 && g2 < 60)
    }

    @Test
    fun colourMapBrightnessRisesAcrossTheLowEnd() {
        // Quiet bins must not jump straight to a bright colour.
        var last = -1
        for (k in 0..20) {
            val (r, g, b) = rgb(Levels.colorAt(k / 60f))
            val sum = r + g + b
            assertTrue("t=${k / 60f}", sum >= last)
            last = sum
        }
        val (r, g, b) = rgb(Levels.colorFor(-118f, -120f, -20f))
        assertTrue("near floor is dark", r + g + b < 40)
    }

    @Test
    fun defaultRangeAndClamp() {
        assertEquals(-120f, Levels.DEFAULT_MIN_DB, 0f)
        assertEquals(-20f, Levels.DEFAULT_MAX_DB, 0f)
        // Above 0 dBFS is allowed up to CEIL_DB, for a positive calibration offset.
        assertEquals(-140f to 30f, Levels.clampRange(-200f, 30f))
        assertEquals(-140f to Levels.CEIL_DB, Levels.clampRange(-200f, 90f))
        val (lo, hi) = Levels.clampRange(-50f, -48f)
        assertEquals(Levels.MIN_SPAN_DB, hi - lo, 0.001f)
        val (lo2, hi2) = Levels.clampRange(Levels.CEIL_DB - 3f, Levels.CEIL_DB - 1f)
        assertEquals(Levels.CEIL_DB, hi2, 0f)
        assertEquals(Levels.CEIL_DB - Levels.MIN_SPAN_DB, lo2, 0f)
    }

    @Test
    fun autoRangeTracksFloorAndPeak() {
        val ar = AutoRange()
        assertEquals(Levels.DEFAULT_MIN_DB to Levels.DEFAULT_MAX_DB, ar.range())
        // Room noise at -100 dB with a tone at -40 dB.
        val spec = FloatArray(512) { -100f }.also { it[100] = -40f }
        var r = ar.update(spec, 0f)
        repeat(100) { r = ar.update(spec, 0.05f) }
        assertEquals(-110f, r.first, 0.5f)
        assertEquals(-34f, r.second, 0.5f)
    }

    @Test
    fun autoRangePeakJumpsUpAndRelaxes() {
        val ar = AutoRange(timeConstantS = 1f)
        val quiet = FloatArray(256) { -100f }.also { it[10] = -70f }
        val loud = quiet.copyOf().also { it[10] = -10f }
        ar.update(quiet, 0f)
        val up = ar.update(loud, 0.02f)
        assertEquals(-4f, up.second, 0.01f) // instant attack, no clipping
        var r = up
        repeat(200) { r = ar.update(quiet, 0.05f) }
        assertTrue("relaxed back down, got ${r.second}", r.second < -50f)
    }

    @Test
    fun autoRangeKeepsAMinimumSpanForPureNoise() {
        val ar = AutoRange()
        val flat = FloatArray(128) { -90f }
        val (lo, hi) = ar.update(flat, 0f)
        assertEquals(-100f, lo, 0.01f)
        assertEquals(-50f, hi, 0.01f)
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

class MicSettingsTest {
    @Test
    fun micIsTheDefaultAndUnprocessedIsOnlyTriedWhenPicked() {
        assertEquals(MicSettings.Source.MIC, MicSettings.Source.fromId(null))
        assertEquals(MicSettings.Source.MIC, MicSettings.Source.fromId("bogus"))
        val def = MicSettings.candidateOrder(MicSettings.Source.MIC, 34)
        assertEquals(MicSettings.Source.MIC, def.first())
        assertFalse(MicSettings.Source.UNPROCESSED in def)
        val un = MicSettings.candidateOrder(MicSettings.Source.UNPROCESSED, 34)
        assertEquals(MicSettings.Source.UNPROCESSED, un.first())
        assertEquals(MicSettings.Source.MIC, un[1])
    }

    @Test
    fun candidateOrderHasNoRepeatsAndRespectsSdk() {
        for (s in MicSettings.Source.PICKABLE) {
            val order = MicSettings.candidateOrder(s, 34)
            assertEquals(order.size, order.toSet().size)
            assertEquals(MicSettings.Source.DEFAULT, order.last())
            assertEquals(s.id, MicSettings.Source.fromId(s.id).id)
        }
        assertFalse(MicSettings.Source.UNPROCESSED in MicSettings.candidateOrder(MicSettings.Source.UNPROCESSED, 23))
    }

    @Test
    fun gainFactorAndPcmConversion() {
        assertEquals(1f, MicSettings.gainFactor(0f), 1e-6f)
        assertEquals(10f, MicSettings.gainFactor(20f), 1e-4f)
        assertEquals(MicSettings.gainFactor(30f), MicSettings.gainFactor(99f), 0f)
        assertEquals(1f, MicSettings.gainFactor(-5f), 0f)
        val src = shortArrayOf(16384, -32768, 0)
        val dst = FloatArray(3)
        MicSettings.pcmToFloat(src, dst, 3, 1f)
        assertEquals(0.5f, dst[0], 1e-6f)
        assertEquals(-1f, dst[1], 1e-6f)
        MicSettings.pcmToFloat(src, dst, 3, MicSettings.gainFactor(20f))
        assertEquals(5f, dst[0], 1e-3f) // boosted, not clipped
    }

    @Test
    fun twentyDbGainRaisesASineByTwentyDb() {
        val n = 2048
        val w = Fft.hann(n)
        val pcm = ShortArray(n) { i -> (3276.8 * sin(2.0 * PI * 128 * i / n)).toInt().toShort() }
        fun peak(gainDb: Float): Float {
            val x = FloatArray(n)
            MicSettings.pcmToFloat(pcm, x, n, MicSettings.gainFactor(gainDb))
            val re = FloatArray(n) { x[it] * w[it] }
            val im = FloatArray(n)
            Fft.forward(re, im)
            val mags = FloatArray(n / 2)
            Fft.magnitudeDb(re, im, mags, Fft.windowSum(w))
            return mags.maxOrNull()!!
        }
        assertEquals(-20f, peak(0f), 0.5f)
        assertEquals(20f, peak(20f) - peak(0f), 0.05f)
    }
}

class SplitLayoutTest {
    @Test
    fun evenSplitGivesHalfTheHeightToEach() {
        val l = SpectrumControls.splitLayout(1000, SpectrumControls.SplitRatio.EVEN)
        assertEquals(500, l.spectrumHeight)
        assertEquals(508, l.waterfallTop)
        assertEquals(492, l.waterfallHeight)
        assertEquals(1000, l.waterfallTop + l.waterfallHeight)
        assertEquals(SpectrumControls.SplitRatio.EVEN, SpectrumControls.SplitRatio.DEFAULT)
    }

    @Test
    fun otherRatiosAndTinyHeights() {
        assertEquals(400, SpectrumControls.splitLayout(1000, SpectrumControls.SplitRatio.SPECTRUM_SMALL).spectrumHeight)
        assertEquals(600, SpectrumControls.splitLayout(1000, SpectrumControls.SplitRatio.SPECTRUM_LARGE).spectrumHeight)
        val tiny = SpectrumControls.splitLayout(10, SpectrumControls.SplitRatio.EVEN)
        assertTrue(tiny.waterfallHeight >= 0 && tiny.waterfallTop <= 10)
        val zero = SpectrumControls.splitLayout(0, SpectrumControls.SplitRatio.EVEN)
        assertEquals(0, zero.waterfallHeight)
        for (r in SpectrumControls.SplitRatio.values()) {
            assertEquals(r, SpectrumControls.SplitRatio.fromId(r.id))
        }
        assertEquals(SpectrumControls.SplitRatio.EVEN, SpectrumControls.SplitRatio.fromId("nope"))
    }

    @Test
    fun oneRowPerPixelWithinCaps() {
        assertEquals(492, SpectrumControls.waterfallRowsFor(492))
        assertEquals(SpectrumControls.MIN_WATERFALL_ROWS, SpectrumControls.waterfallRowsFor(3))
        assertEquals(SpectrumControls.MAX_WATERFALL_ROWS, SpectrumControls.waterfallRowsFor(99_999))
    }

    @Test
    fun ringSegmentsListRowsNewestToOldest() {
        assertEquals(listOf(0 until 5), SpectrumControls.ringSegments(0, 5))
        assertEquals(listOf(3 until 5, 0 until 3), SpectrumControls.ringSegments(3, 5))
        assertEquals(listOf(4 until 5, 0 until 4), SpectrumControls.ringSegments(-1, 5))
        for (head in 0 until 7) {
            val order = SpectrumControls.ringSegments(head, 7).flatMap { it.toList() }
            assertEquals(7, order.size)
            assertEquals(head, order.first())
            assertEquals((0 until 7).toSet(), order.toSet())
        }
    }

    @Test
    fun rowPlanForPixelRowsStillCoversTheSpan() {
        val rows = SpectrumControls.waterfallRowsFor(1200)
        for (d in SpectrumControls.WaterfallDuration.values()) {
            val plan = SpectrumControls.rowPlan(d.seconds, rows, 48_000, 2048)
            val covered = plan.rowSeconds(48_000) * rows
            assertTrue("${d.id}: $covered s", covered >= d.seconds * 0.9f)
        }
    }
}

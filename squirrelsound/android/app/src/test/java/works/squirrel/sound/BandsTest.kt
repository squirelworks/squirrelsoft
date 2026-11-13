package works.squirrel.sound

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.sin

class BandsTest {
    @Test
    fun octaveCentresAreBaseTen() {
        val set = Bands.octaveBands(1, 20.0, 16000.0)
        val want = doubleArrayOf(31.62, 63.10, 125.89, 251.19, 501.19, 1000.0, 1995.26, 3981.07, 7943.28, 15848.93)
        assertEquals(want.size, set.size)
        for (i in want.indices) assertEquals(want[i], set.centre[i], 0.01)
        // edges meet: each band's top is the next band's bottom
        for (i in 0 until set.size - 1) assertEquals(set.hi[i], set.lo[i + 1], 1e-9)
        assertEquals(1000.0 / Math.pow(10.0, 0.15), set.lo[5], 1e-9)
    }

    @Test
    fun thirdOctavesAndEdgeCases() {
        // the 20 Hz band's exact centre is 19.95 Hz, so a 20 Hz floor starts at 25
        assertEquals(30, Bands.octaveBands(3, 20.0, 20000.0).size)
        val set = Bands.octaveBands(3, 19.0, 20000.0)
        assertEquals(31, set.size) // 20 Hz ... 20 kHz nominal
        assertEquals(19.95, set.centre[0], 0.01)
        assertEquals(0, Bands.octaveBands(0, 20.0, 100.0).size)
        assertEquals(0, Bands.octaveBands(1, 200.0, 100.0).size)
        assertTrue(Bands.octaveBands(3, 0.0, 1e9).size <= Bands.MAX_BANDS)
        // without the native core SqDsp gives the Kotlin bands
        assertFalse(SqDsp.available)
        assertArrayEquals(set.centre, SqDsp.octaveBands(3, 19.0, 20000.0).centre, 0.0)
    }

    @Test
    fun aFullScaleSineReadsZeroInItsBand() {
        val n = 4096
        val sr = 48000
        val w = Fft.hann(n)
        val hz = 1000.0 * sr / sr // 1 kHz, between bins
        val re = FloatArray(n) { (sin(2.0 * PI * hz * it / sr) * w[it]).toFloat() }
        val db = FloatArray(n / 2)
        SqDsp.spectrumDbOrKotlin(re, FloatArray(n), Fft.windowSum(w), db)
        val set = Bands.octaveBands(1, 20.0, 16000.0)
        val out = FloatArray(set.size)
        val enbw = Bands.enbw(w)
        assertEquals(1.5, enbw, 0.01)
        SqDsp.bandLevels(db, sr.toDouble() / n, set, enbw, out)
        assertEquals(0f, out[5], 0.2f) // the 1 kHz band
        assertTrue(out[3] < -40f) // 250 Hz: only leakage
        assertTrue(out.all { it >= Levels.FLOOR_DB })
    }

    @Test
    fun emptyBandsGetTheFloor() {
        val set = Bands.octaveBands(3, 20.0, 40.0) // narrower than one bin at 48k/1024
        val out = FloatArray(set.size)
        Bands.levels(FloatArray(512) { -20f }, 48000.0 / 1024, set, 1.5, out)
        assertTrue(out.any { it == Levels.FLOOR_DB })
    }

    @Test
    fun labelsAndStride() {
        assertEquals("31.6", Bands.label(31.62))
        assertEquals("1k", Bands.label(1000.0))
        assertEquals("12.6k", Bands.label(12589.25))
        assertEquals("126", Bands.label(125.89))
        assertEquals("2k", Bands.label(1995.26))
        assertEquals(1, SpectrumControls.labelStride(10, 1000f, 60f))
        assertEquals(3, SpectrumControls.labelStride(31, 700f, 60f))
        assertEquals(SpectrumControls.ViewMode.THIRD_OCTAVE, SpectrumControls.ViewMode.fromId("third"))
        assertEquals(SpectrumControls.ViewMode.SPECTRUM, SpectrumControls.ViewMode.fromId("nope"))
        assertFalse(SpectrumControls.ViewMode.SPECTRUM.isBands)
    }
}

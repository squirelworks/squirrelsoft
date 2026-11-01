package works.squirrel.sound

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
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
        assertTrue("peak near 440, got $hz", kotlin.math.abs(hz - 440.0) < 30.0)
        assertEquals(n, w.size)
    }
}

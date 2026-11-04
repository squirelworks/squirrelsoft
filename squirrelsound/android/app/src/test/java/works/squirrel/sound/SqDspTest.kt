package works.squirrel.sound

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import kotlin.math.PI
import kotlin.math.sin

class SqDspTest {
    @Test
    fun withoutNativeCoreFallsBackToKotlin() {
        // JVM unit tests have no libsqdsp_jni.so.
        assertFalse(SqDsp.available)
        val n = 1024
        val w = Fft.hann(n)
        val ws = Fft.windowSum(w)
        val frame = FloatArray(n) { (sin(2.0 * PI * 64 * it / n) * w[it]).toFloat() }
        assertFalse(SqDsp.spectrumDb(frame.copyOf(), ws, FloatArray(n / 2)))

        val re = frame.copyOf()
        val im = FloatArray(n)
        val got = FloatArray(n / 2)
        SqDsp.spectrumDbOrKotlin(re, im, ws, got)

        val re2 = frame.copyOf()
        val im2 = FloatArray(n)
        val want = FloatArray(n / 2)
        Fft.forward(re2, im2)
        Fft.magnitudeDb(re2, im2, want, ws)
        assertArrayEquals(want, got, 0f)
        // A full-scale sine on bin 64 reads 0 dBFS.
        assertEquals(0f, got[64], 0.05f)
    }
}

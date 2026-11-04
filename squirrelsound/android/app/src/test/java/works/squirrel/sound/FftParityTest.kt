package works.squirrel.sound

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.math.abs
import kotlin.math.sqrt

/**
 * Kotlin Fft.kt must agree with the C core libs/sqdsp, the shared DSP
 * library, on the same input. The fixture is written by
 * libs/sqdsp/tools/gen_parity.c (make -C libs/sqdsp fixture) and is checked
 * against the C code by make test.
 */
class FftParityTest {
    private fun fixture(): File {
        // Gradle runs unit tests from the module dir (squirrelsound/android/app);
        // walk up to the repo root rather than hard-coding the depth.
        var dir: File? = File(".").absoluteFile
        while (dir != null) {
            val f = File(dir, "libs/sqdsp/fixtures/parity_1024.txt")
            if (f.isFile) return f
            dir = dir.parentFile
        }
        error("libs/sqdsp/fixtures/parity_1024.txt not found above ${File(".").absolutePath}")
    }

    @Test
    fun hannAndMagnitudesMatchTheCCore() {
        val v = fixture().readLines().filter { it.isNotBlank() }.map { it.trim().toDouble() }
        val n = v[0].toInt()
        assertEquals(1024, n)
        val x = v.subList(1, 1 + n)
        val wC = v.subList(1 + n, 1 + 2 * n)
        val magC = v.subList(1 + 2 * n, 1 + 2 * n + n / 2 + 1)

        val w = Fft.hann(n)
        for (i in 0 until n) assertEquals("window[$i]", wC[i], w[i].toDouble(), 1e-6)

        val re = FloatArray(n) { (x[it] * w[it]).toFloat() }
        val im = FloatArray(n)
        Fft.forward(re, im)

        val peak = magC.maxOrNull()!!
        assertTrue(peak > 1.0)
        var worst = 0.0
        for (k in 0..n / 2) {
            val m = sqrt((re[k] * re[k] + im[k] * im[k]).toDouble())
            worst = maxOf(worst, abs(m - magC[k]) / peak)
        }
        assertTrue("worst relative difference $worst", worst < 1e-4)
    }
}

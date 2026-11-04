package works.squirrel.sound

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/** In-place radix-2 Cooley–Tukey FFT (clean-room). */
object Fft {
    fun forward(re: FloatArray, im: FloatArray) {
        val n = re.size
        require(n == im.size && n >= 2 && n and (n - 1) == 0) { "power-of-two length" }

        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) {
                j = j xor bit
                bit = bit shr 1
            }
            j = j xor bit
            if (i < j) {
                val tr = re[i]; re[i] = re[j]; re[j] = tr
                val ti = im[i]; im[i] = im[j]; im[j] = ti
            }
        }

        var len = 2
        while (len <= n) {
            val ang = (-2.0 * PI / len).toFloat()
            val wlenRe = cos(ang.toDouble()).toFloat()
            val wlenIm = sin(ang.toDouble()).toFloat()
            var i = 0
            while (i < n) {
                var wRe = 1f
                var wIm = 0f
                for (k in 0 until len / 2) {
                    val uRe = re[i + k]
                    val uIm = im[i + k]
                    val vRe = re[i + k + len / 2] * wRe - im[i + k + len / 2] * wIm
                    val vIm = re[i + k + len / 2] * wIm + im[i + k + len / 2] * wRe
                    re[i + k] = uRe + vRe
                    im[i + k] = uIm + vIm
                    re[i + k + len / 2] = uRe - vRe
                    im[i + k + len / 2] = uIm - vIm
                    val nextWRe = wRe * wlenRe - wIm * wlenIm
                    wIm = wRe * wlenIm + wIm * wlenRe
                    wRe = nextWRe
                }
                i += len
            }
            len = len shl 1
        }
    }

    /**
     * One-sided magnitude spectrum in dBFS. [windowSum] is the sum of the window
     * coefficients applied before [forward]; bins are scaled by 2/windowSum (DC by
     * 1/windowSum) so a full-scale sine reads 0 dBFS whatever the window or size.
     */
    fun magnitudeDb(
        re: FloatArray,
        im: FloatArray,
        outDb: FloatArray,
        windowSum: Float,
        floorDb: Float = Levels.FLOOR_DB
    ) {
        val n = outDb.size
        require(n <= re.size / 2 + 1)
        require(windowSum > 0f) { "windowSum must be > 0" }
        val half = re.size / 2
        for (i in 0 until n) {
            val scale = if (i == 0 || i == half) 1f / windowSum else 2f / windowSum
            val mag = sqrt(re[i] * re[i] + im[i] * im[i]) * scale
            val db = if (mag > 1e-12f) (20f * kotlin.math.log10(mag)) else floorDb
            outDb[i] = db.coerceAtLeast(floorDb)
        }
    }

    /** Sum of window coefficients (coherent gain × N), for [magnitudeDb]. */
    fun windowSum(w: FloatArray): Float {
        var s = 0.0
        for (v in w) s += v
        return s.toFloat()
    }

    fun hann(n: Int): FloatArray {
        val w = FloatArray(n)
        if (n == 1) {
            w[0] = 1f
            return w
        }
        for (i in 0 until n) {
            w[i] = (0.5 * (1.0 - cos(2.0 * PI * i / (n - 1)))).toFloat()
        }
        return w
    }
}

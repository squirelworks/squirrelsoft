package works.squirrel.sound

import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.pow

/**
 * Fractional-octave bands (IEC 61260-1, base 10) and their levels: the Kotlin
 * twin of sqdsp_octave_bands / sqdsp_band_levels_db in libs/sqdsp. [SqDsp]
 * uses the C core when it is loaded; this runs in JVM tests and as the
 * fallback. Keep the two in step.
 */
object Bands {
    /** More than the third-octave bands from 1 Hz to 100 kHz. */
    const val MAX_BANDS = 64

    /** Centres and edges (Hz) of [size] bands. */
    class Set(val fraction: Int, val centre: DoubleArray, val lo: DoubleArray, val hi: DoubleArray) {
        val size: Int get() = centre.size
    }

    private val G = 10.0.pow(0.3)

    fun octaveBands(fraction: Int, fmin: Double, fmax: Double): Set {
        if (fraction < 1 || !(fmax >= fmin) || fmax <= 0) return Set(fraction, DoubleArray(0), DoubleArray(0), DoubleArray(0))
        val b = fraction.toDouble()
        val shift = if (fraction % 2 != 0) 0.0 else 0.5
        val loF = if (fmin > 1e-3) fmin else 1e-3
        val x0 = ceil(b * ln(loF / 1000.0) / ln(G) - shift - 1e-9).toLong()
        val x1 = floor(b * ln(fmax / 1000.0) / ln(G) - shift + 1e-9).toLong()
        val n = (x1 - x0 + 1).coerceIn(0, MAX_BANDS.toLong()).toInt()
        val e = G.pow(1.0 / (2.0 * b))
        val c = DoubleArray(n) { 1000.0 * G.pow(((x0 + it).toDouble() + shift) / b) }
        return Set(fraction, c, DoubleArray(n) { c[it] / e }, DoubleArray(n) { c[it] * e })
    }

    /** n * sum(w^2) / sum(w)^2: 1.0 for a rectangular window, 1.5 for Hann. */
    fun enbw(w: FloatArray): Double {
        var s = 0.0
        var s2 = 0.0
        for (v in w) {
            s += v
            s2 += v.toDouble() * v
        }
        return if (s != 0.0) w.size * s2 / (s * s) else 1.0
    }

    fun levels(magDb: FloatArray, binHz: Double, bands: Set, enbw: Double, out: FloatArray,
               floorDb: Float = Levels.FLOOR_DB) {
        val e = if (enbw > 0) enbw else 1.0
        for (j in 0 until bands.size) {
            // bins with lo <= k * binHz < hi
            val k0 = ceil(bands.lo[j] / binHz).toInt().coerceAtLeast(0)
            var p = 0.0
            var any = false
            var k = k0
            while (k < magDb.size && k * binHz < bands.hi[j]) {
                if (k * binHz >= bands.lo[j]) {
                    p += 10.0.pow(magDb[k] / 10.0)
                    any = true
                }
                k++
            }
            val db = if (any && p > 0) (10.0 * log10(p / e)).toFloat() else floorDb
            out[j] = if (db < floorDb) floorDb else db
        }
    }

    /** "31.6", "1k", "12.6k": a short label for a centre frequency (3 significant digits). */
    fun label(hz: Double): String {
        val v = if (hz >= 1000) hz / 1000 else hz
        val digits = if (v >= 100) 0 else if (v >= 10) 1 else 2
        var t = String.format(java.util.Locale.ROOT, "%." + digits + "f", v)
        if (t.contains('.')) t = t.trimEnd('0').trimEnd('.')
        return if (hz >= 1000) t + "k" else t
    }
}

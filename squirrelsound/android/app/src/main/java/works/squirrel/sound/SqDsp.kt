package works.squirrel.sound

/**
 * Kotlin side of the JNI bridge to the shared C DSP core (libs/sqdsp), the
 * same code the desktop SquirrelSound runs. [Fft] stays as the fallback when
 * the native library isn't there (JVM unit tests) and as the reference the
 * parity test checks the C core against.
 */
object SqDsp {
    /** False when the native library is missing (e.g. JVM unit tests). */
    val available: Boolean by lazy {
        try {
            System.loadLibrary("sqdsp_jni")
            true
        } catch (_: UnsatisfiedLinkError) {
            false
        }
    }

    @JvmStatic
    private external fun nativeSpectrumDb(windowed: FloatArray, windowSum: Float, floorDb: Float, outDb: FloatArray): Boolean

    /**
     * One-sided dBFS spectrum of an already-windowed power-of-two frame, scaled
     * like [Fft.magnitudeDb]. Leaves [windowed] untouched. Returns false (and
     * writes nothing) when the native core isn't loaded or the sizes don't fit;
     * the caller then uses [Fft].
     */
    fun spectrumDb(windowed: FloatArray, windowSum: Float, outDb: FloatArray, floorDb: Float = Levels.FLOOR_DB): Boolean {
        if (!available) return false
        return nativeSpectrumDb(windowed, windowSum, floorDb, outDb)
    }

    @JvmStatic
    private external fun nativeOctaveBands(
        fraction: Int, fmin: Double, fmax: Double, centre: DoubleArray, lo: DoubleArray, hi: DoubleArray,
    ): Int

    @JvmStatic
    private external fun nativeBandLevels(
        magDb: FloatArray, binHz: Double, lo: DoubleArray, hi: DoubleArray, nbands: Int,
        enbw: Double, floorDb: Double, outDb: FloatArray,
    ): Boolean

    /** IEC 61260-1 bands ([fraction] 1 = octave, 3 = third octave) centred in [fmin]..[fmax]: libs/sqdsp, else [Bands]. */
    fun octaveBands(fraction: Int, fmin: Double, fmax: Double): Bands.Set {
        if (available) {
            val cap = Bands.MAX_BANDS
            val c = DoubleArray(cap)
            val lo = DoubleArray(cap)
            val hi = DoubleArray(cap)
            val n = nativeOctaveBands(fraction, fmin, fmax, c, lo, hi).coerceIn(0, cap)
            return Bands.Set(fraction, c.copyOf(n), lo.copyOf(n), hi.copyOf(n))
        }
        return Bands.octaveBands(fraction, fmin, fmax)
    }

    /** Band levels of a one-sided dB spectrum (bin k at k * [binHz]) into [out]: libs/sqdsp, else [Bands]. */
    fun bandLevels(magDb: FloatArray, binHz: Double, bands: Bands.Set, enbw: Double, out: FloatArray,
                   floorDb: Float = Levels.FLOOR_DB) {
        val n = bands.size
        if (available && out.size >= n &&
            nativeBandLevels(magDb, binHz, bands.lo, bands.hi, n, enbw, floorDb.toDouble(), out)
        ) return
        Bands.levels(magDb, binHz, bands, enbw, out, floorDb)
    }

    /** [SqDsp.spectrumDb] when it can, otherwise [Fft] (which overwrites [re] and [im]). */
    fun spectrumDbOrKotlin(re: FloatArray, im: FloatArray, windowSum: Float, outDb: FloatArray) {
        if (spectrumDb(re, windowSum, outDb)) return
        Fft.forward(re, im)
        Fft.magnitudeDb(re, im, outDb, windowSum)
    }

    data class Peak(val hz: Double, val db: Double)

    @JvmStatic
    private external fun nativePeaks(
        magDb: FloatArray,
        sampleRate: Double,
        fftSize: Int,
        fMin: Double,
        fMax: Double,
        dbMin: Double,
        maxN: Int,
        minSepHz: Double,
        outHz: DoubleArray,
        outDb: DoubleArray,
    ): Int

    /**
     * Loudest local maxima of a dB spectrum inside [fMin]..[fMax], via
     * `sqdsp_local_peaks` (the same logic as desktop `live_engine_peaks`).
     * Empty when the native core is missing — do not reimplement in Kotlin.
     *
     * Pass the display floor as [dbMin] (e.g. [SpectrumView.dbMin]). The JNI
     * bridge raises the peak threshold by +10 dB internally (`dbMin + 10`),
     * matching desktop `live_engine_peaks` — do **not** add +10 on the Kotlin
     * side or peaks will be over-thresholded.
     */
    fun peaks(
        magDb: FloatArray,
        sampleRate: Double,
        fftSize: Int,
        fMin: Double,
        fMax: Double,
        dbMin: Double,
        maxN: Int = MAX_PEAKS,
        minSepHz: Double = Double.NaN,
    ): List<Peak> {
        if (!available || magDb.size < 3 || maxN <= 0 || !(sampleRate > 0) || fftSize <= 0) {
            return emptyList()
        }
        val sep = if (minSepHz.isNaN()) (fMax - fMin) / 40.0 else minSepHz
        val outHz = DoubleArray(maxN)
        val outDb = DoubleArray(maxN)
        val n = nativePeaks(magDb, sampleRate, fftSize, fMin, fMax, dbMin, maxN, sep, outHz, outDb)
            .coerceIn(0, maxN)
        if (n <= 0) return emptyList()
        return List(n) { Peak(outHz[it], outDb[it]) }
    }

    const val MAX_PEAKS = 8
}

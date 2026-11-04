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

    /** [SqDsp.spectrumDb] when it can, otherwise [Fft] (which overwrites [re] and [im]). */
    fun spectrumDbOrKotlin(re: FloatArray, im: FloatArray, windowSum: Float, outDb: FloatArray) {
        if (spectrumDb(re, windowSum, outDb)) return
        Fft.forward(re, im)
        Fft.magnitudeDb(re, im, outDb, windowSum)
    }
}

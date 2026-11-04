package works.squirrel.sound

/** One analyzed FFT frame pushed to the UI. */
data class SpectrumFrame(
    val db: FloatArray,
    val sampleRate: Int,
    val fftSize: Int,
    val peakHz: Float,
    val peakDb: Float,
    val rmsDb: Float,
    val sourceLabel: String,
    /** A finished LOFARgram row (dB) when this frame completes one, else null. */
    val waterfallRow: FloatArray? = null,
)

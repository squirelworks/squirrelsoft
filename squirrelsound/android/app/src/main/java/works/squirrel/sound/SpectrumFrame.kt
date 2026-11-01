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
)

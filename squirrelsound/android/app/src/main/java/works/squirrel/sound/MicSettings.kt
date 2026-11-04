package works.squirrel.sound

import kotlin.math.pow

/**
 * Mic source choice and digital gain. Free of Android types so JVM tests can
 * cover the fallback order and gain math; [AudioEngine] maps sources to
 * MediaRecorder.AudioSource constants.
 */
object MicSettings {
    /**
     * Capture paths the user can pick. MIC is the default: on many phones
     * UNPROCESSED has no gain stage and reads 10–20 dB quieter.
     */
    enum class Source(val id: String, val label: String, val minSdk: Int = 1) {
        MIC("mic", "mic"),
        UNPROCESSED("unprocessed", "mic-unprocessed", minSdk = 24),
        CAMCORDER("camcorder", "mic-camcorder"),
        VOICE_RECOGNITION("voice", "mic-voice"),
        DEFAULT("default", "mic-default");

        companion object {
            /** Sources offered in the picker, in picker order. */
            val PICKABLE = listOf(MIC, UNPROCESSED, CAMCORDER, VOICE_RECOGNITION)
            val PREFERRED = MIC

            fun fromId(id: String?): Source =
                PICKABLE.firstOrNull { it.id == id } ?: PREFERRED
        }
    }

    /**
     * Order to try when opening the mic: the chosen source first, then the
     * usual fallbacks (MIC, CAMCORDER, VOICE_RECOGNITION, DEFAULT), skipping
     * any the device's [sdkInt] cannot open and never repeating one.
     * UNPROCESSED is only tried when picked, since it is the quiet path.
     */
    fun candidateOrder(chosen: Source, sdkInt: Int): List<Source> {
        val order = listOf(
            chosen,
            Source.MIC,
            Source.CAMCORDER,
            Source.VOICE_RECOGNITION,
            Source.DEFAULT
        )
        return order.distinct().filter { sdkInt >= it.minSdk }
    }

    const val MAX_GAIN_DB = 30f

    /** Linear factor for a digital gain in dB, clamped to 0..MAX_GAIN_DB. */
    fun gainFactor(gainDb: Float): Float =
        10f.pow(gainDb.coerceIn(0f, MAX_GAIN_DB) / 20f)

    /**
     * Converts 16-bit PCM to floats in -1..1 times [factor]. Values are not
     * clipped: the spectrum shows the boosted level (it can read above 0 dBFS)
     * without adding clipping harmonics.
     */
    fun pcmToFloat(src: ShortArray, dst: FloatArray, count: Int, factor: Float) {
        val scale = factor / 32768f
        for (i in 0 until count) dst[i] = src[i] * scale
    }
}

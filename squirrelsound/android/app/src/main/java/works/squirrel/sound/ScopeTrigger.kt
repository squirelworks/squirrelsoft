package works.squirrel.sound

/**
 * Rising-edge trigger and auto timebase for the Android scope view.
 * Matches the desktop Live scope: start on the first rising zero crossing so a
 * steady tone stands still, and show half the FFT frame (auto timebase).
 */
object ScopeTrigger {

    /** Samples shown after the trigger; half the FFT, at least 1. */
    fun autoSpan(fftSize: Int): Int = (fftSize / 2).coerceAtLeast(1)

    /**
     * Index of the first rising zero crossing such that [span] samples still
     * fit after it. Returns 0 when none is found (draw from the start).
     */
    fun risingEdgeStart(samples: FloatArray, span: Int): Int {
        val n = samples.size
        if (n < 2 || span <= 0) return 0
        val maxStart = (n - span).coerceAtLeast(0)
        for (i in 1..maxStart) {
            if (samples[i - 1] < 0f && samples[i] >= 0f) return i
        }
        return 0
    }

    /** Milliseconds covered by [span] samples at [sampleRate]. */
    fun spanMs(span: Int, sampleRate: Int): Float {
        if (sampleRate <= 0 || span <= 0) return 0f
        return 1000f * span / sampleRate
    }
}

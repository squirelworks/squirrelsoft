package works.squirrel.sound

import kotlin.math.max
import kotlin.math.min

/**
 * Frequency-window presets, zoom math, and waterfall scroll → hop mapping.
 * Kept free of Android UI types so unit tests can cover the product defaults.
 */
object SpectrumControls {

    /** Default open range: human hearing, phone-friendly upper bound. */
    const val DEFAULT_FMIN_HZ = 20f
    const val DEFAULT_FMAX_HZ = 16_000f
    const val MIN_SPAN_HZ = 40f

    enum class RangePreset(
        val id: String,
        /** Inclusive lower bound (Hz). */
        val fMinHz: Float,
        /**
         * Inclusive upper bound (Hz), or null to use Nyquist (sampleRate/2).
         * Music keeps bass in-frame by starting at 20 Hz with a mid-focused top.
         */
        val fMaxHz: Float?
    ) {
        HUMAN("human", 20f, 16_000f),
        MUSIC("music", 20f, 4_000f),
        BASS("bass", 20f, 250f),
        SPEECH("speech", 100f, 8_000f),
        FULL("full", 20f, null);

        fun resolve(nyquistHz: Float): Pair<Float, Float> {
            val ny = nyquistHz.coerceAtLeast(MIN_SPAN_HZ + 1f)
            val hiTarget = fMaxHz?.coerceAtMost(ny) ?: ny
            return clampRange(fMinHz, hiTarget, ny)
        }
    }

    /** Waterfall / analysis hop. Smaller hop → faster scroll. */
    enum class ScrollSpeed(val id: String, val hopMs: Int) {
        SLOW("slow", 50),
        NORMAL("normal", 20),
        FAST("fast", 10);

        companion object {
            fun fromHopMs(hopMs: Int): ScrollSpeed = when {
                hopMs >= 40 -> SLOW
                hopMs <= 12 -> FAST
                else -> NORMAL
            }
        }
    }

    fun clampRange(fMin: Float, fMax: Float, nyquistHz: Float): Pair<Float, Float> {
        val ny = nyquistHz.coerceAtLeast(MIN_SPAN_HZ + 1f)
        var lo = fMin.coerceIn(0f, ny - MIN_SPAN_HZ)
        var hi = fMax.coerceIn(lo + MIN_SPAN_HZ, ny)
        if (hi - lo < MIN_SPAN_HZ) {
            hi = min(ny, lo + MIN_SPAN_HZ)
            lo = max(0f, hi - MIN_SPAN_HZ)
        }
        return lo to hi
    }

    /**
     * Zoom about the window midpoint.
     * @param factor &gt;1 zooms in (narrower), &lt;1 zooms out (wider).
     */
    fun zoom(
        fMin: Float,
        fMax: Float,
        factor: Float,
        nyquistHz: Float
    ): Pair<Float, Float> {
        val f = factor.coerceIn(0.05f, 20f)
        val mid = (fMin + fMax) * 0.5f
        val half = ((fMax - fMin) * 0.5f) / f
        return clampRange(mid - half, mid + half, nyquistHz)
    }

    /** Pan the window by a fraction of its span (−1…+1 typical). */
    fun pan(
        fMin: Float,
        fMax: Float,
        fractionOfSpan: Float,
        nyquistHz: Float
    ): Pair<Float, Float> {
        val span = (fMax - fMin).coerceAtLeast(MIN_SPAN_HZ)
        val delta = span * fractionOfSpan
        return clampRange(fMin + delta, fMax + delta, nyquistHz)
    }

    fun hopMsForScroll(speed: ScrollSpeed): Int = speed.hopMs.coerceIn(5, 200)
}

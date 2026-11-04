package works.squirrel.sound

import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

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

    /** Rows in the LOFARgram (waterfall) bitmap. */
    const val WATERFALL_ROWS = 160

    /** How much time the visible LOFARgram covers, top to bottom. */
    enum class WaterfallDuration(val id: String, val seconds: Float) {
        S5("5s", 5f),
        S15("15s", 15f),
        S30("30s", 30f),
        S60("60s", 60f),
        M2("2min", 120f),
        M5("5min", 300f);

        companion object {
            val DEFAULT = S15
        }
    }

    /**
     * Analysis hop (samples between FFT frames) and how many frames are averaged
     * into one waterfall row.
     */
    data class RowPlan(val hopSamples: Int, val framesPerRow: Int) {
        fun rowSeconds(sampleRate: Int): Float = hopSamples.toFloat() * framesPerRow / sampleRate
    }

    /**
     * Plan the framing so [rows] rows span [durationS] seconds. The hop never
     * exceeds [fftSize] (no samples skipped); long durations average several
     * frames per row instead. Very short durations are limited by the minimum hop
     * of fftSize / 16.
     */
    fun rowPlan(durationS: Float, rows: Int, sampleRate: Int, fftSize: Int): RowPlan {
        require(sampleRate > 0 && fftSize > 0)
        val rowSamples = durationS.coerceAtLeast(0.1f).toDouble() * sampleRate / rows.coerceAtLeast(1)
        val frames = ceil(rowSamples / fftSize).toInt().coerceAtLeast(1)
        val minHop = (fftSize / 16).coerceAtLeast(1)
        val hop = (rowSamples / frames).roundToInt().coerceIn(minHop, fftSize)
        return RowPlan(hop, frames)
    }

    /** Time-axis label for [seconds] ago on the LOFARgram. */
    fun timeLabel(seconds: Float): String = when {
        seconds <= 0f -> "now"
        seconds >= 60f && seconds % 60f == 0f -> "-${(seconds / 60f).toInt()}m"
        seconds >= 60f -> "-${"%.1f".format(seconds / 60f)}m"
        seconds == seconds.toInt().toFloat() -> "-${seconds.toInt()}s"
        else -> "-${"%.1f".format(seconds)}s"
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
}

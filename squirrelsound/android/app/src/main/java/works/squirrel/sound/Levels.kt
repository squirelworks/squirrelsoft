package works.squirrel.sound

import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** dBFS display range, colour map and auto-range. Free of Android types for JVM tests. */
object Levels {
    /** Lowest level the analysis reports (silence / digital zero). */
    const val FLOOR_DB = -140f

    /** Default display range: room noise is visible, speech at arm's length is warm. */
    const val DEFAULT_MIN_DB = -120f
    const val DEFAULT_MAX_DB = -20f

    /** Smallest allowed gap between display min and max. */
    const val MIN_SPAN_DB = 10f

    /** Clamp a display range into FLOOR_DB..0 with at least MIN_SPAN_DB between ends. */
    fun clampRange(minDb: Float, maxDb: Float): Pair<Float, Float> {
        var lo = minDb.coerceIn(FLOOR_DB, -MIN_SPAN_DB)
        var hi = maxDb.coerceIn(FLOOR_DB + MIN_SPAN_DB, 0f)
        if (hi - lo < MIN_SPAN_DB) {
            hi = (lo + MIN_SPAN_DB).coerceAtMost(0f)
            lo = hi - MIN_SPAN_DB
        }
        return lo to hi
    }

    /**
     * Colour stops, black at the floor:
     * black → blue → cyan → green → yellow → red → white.
     */
    private val stops = arrayOf(
        intArrayOf(0, 0, 0),
        intArrayOf(0, 0, 200),
        intArrayOf(0, 190, 255),
        intArrayOf(0, 210, 60),
        intArrayOf(255, 230, 0),
        intArrayOf(255, 30, 0),
        intArrayOf(255, 255, 255),
    )

    /** Opaque ARGB for a position t in 0..1 along the colour map. */
    fun colorAt(t: Float): Int {
        val x = (if (t.isNaN()) 0f else t.coerceIn(0f, 1f)) * (stops.size - 1)
        val i = min(x.toInt(), stops.size - 2)
        val f = x - i
        val a = stops[i]
        val b = stops[i + 1]
        val r = (a[0] + (b[0] - a[0]) * f).roundToInt()
        val g = (a[1] + (b[1] - a[1]) * f).roundToInt()
        val bl = (a[2] + (b[2] - a[2]) * f).roundToInt()
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or bl
    }

    /** Opaque ARGB for [db] shown in the range [minDb]..[maxDb]. */
    fun colorFor(db: Float, minDb: Float, maxDb: Float): Int {
        val span = (maxDb - minDb).coerceAtLeast(1f)
        return colorAt((db - minDb) / span)
    }
}

/**
 * Tracks the noise floor and the peak of recent spectra and proposes a display
 * range: a little below the floor up to a little above the peak.
 *
 * The floor is a low percentile of each spectrum, smoothed over [timeConstantS].
 * The peak rises at once and falls back over [timeConstantS], so a loud sound
 * is never clipped but the range relaxes after it.
 */
class AutoRange(
    private val timeConstantS: Float = 3f,
    private val floorPercentile: Float = 0.2f,
    private val belowFloorDb: Float = 10f,
    private val abovePeakDb: Float = 6f,
    /** The range never gets narrower than this, so pure noise stays dark. */
    private val minSpanDb: Float = 50f,
) {
    private var floor = Float.NaN
    private var peak = Float.NaN
    private var scratch = FloatArray(0)

    fun reset() {
        floor = Float.NaN
        peak = Float.NaN
    }

    /**
     * Feed one spectrum (dB) observed [dtSeconds] after the previous one; returns
     * the proposed (min, max) display range.
     */
    fun update(db: FloatArray, dtSeconds: Float): Pair<Float, Float> {
        if (db.isEmpty()) return range()
        if (scratch.size != db.size) scratch = FloatArray(db.size)
        db.copyInto(scratch)
        scratch.sort()
        val idx = ((scratch.size - 1) * floorPercentile).roundToInt().coerceIn(0, scratch.size - 1)
        val frameFloor = scratch[idx]
        val framePeak = scratch[scratch.size - 1]
        val a = (dtSeconds.coerceAtLeast(0f) / timeConstantS).coerceIn(0f, 1f)
        floor = if (floor.isNaN()) frameFloor else floor + (frameFloor - floor) * a
        peak = when {
            peak.isNaN() || framePeak > peak -> framePeak
            else -> peak + (framePeak - peak) * a
        }
        return range()
    }

    fun range(): Pair<Float, Float> {
        if (floor.isNaN()) return Levels.DEFAULT_MIN_DB to Levels.DEFAULT_MAX_DB
        val lo = floor - belowFloorDb
        val hi = max(peak + abovePeakDb, lo + minSpanDb)
        return Levels.clampRange(lo, hi)
    }
}

package works.squirrel.sound

/**
 * Averages [SpectrumControls.RowPlan.framesPerRow] FFT frames (dB) into one
 * waterfall row, so long LOFARgram durations keep every frame's energy.
 * Row count comes from the LOFARgram pane height (portrait or landscape), one
 * bitmap row per pixel via [SpectrumControls.waterfallRowsFor].
 */
class WaterfallRowAccumulator {
    private var acc: FloatArray? = null
    private var count = 0

    fun reset() {
        acc = null
        count = 0
    }

    /** Add one frame; returns a finished row every [framesPerRow] frames, else null. */
    fun add(frame: FloatArray, framesPerRow: Int): FloatArray? {
        val n = framesPerRow.coerceAtLeast(1)
        var a = acc
        if (a == null || a.size != frame.size) {
            a = FloatArray(frame.size)
            acc = a
            count = 0
        }
        for (i in frame.indices) a[i] += frame[i]
        count++
        if (count < n) return null
        val row = FloatArray(a.size) { a[it] / count }
        a.fill(0f)
        count = 0
        return row
    }
}

package works.squirrel.sound

/**
 * A level offset (dB) per microphone, added to every reading so the app can
 * be matched to a reference meter; the desktop keeps the same per input
 * (`cal.<source>` in live.conf). Stored under "cal.<mic source id>".
 */
object Calibration {
    const val MIN_DB = -60f
    const val MAX_DB = 60f

    fun key(sourceId: String): String = "cal.$sourceId"

    fun clamp(db: Float): Float = if (db.isNaN()) 0f else db.coerceIn(MIN_DB, MAX_DB)

    /** SeekBar position (0..[steps]) for an offset, in 1 dB steps. */
    val steps: Int get() = (MAX_DB - MIN_DB).toInt()

    fun toProgress(db: Float): Int = (clamp(db) - MIN_DB).toInt()

    fun fromProgress(p: Int): Float = clamp(MIN_DB + p)

    /** "cal +3 dB", or "" when there is none. */
    fun label(db: Float): String = if (db == 0f) "" else "cal %+.0f dB".format(java.util.Locale.ROOT, db)
}

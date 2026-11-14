package works.squirrel.suica

/**
 * Saibane station-code → name lookup for Suica/PASMO-class history blocks.
 *
 * Table format matches `squirrelsuica/data/StationCode.csv` (MIT-licensed
 * soltia48/suica-viewer adaptation; see that README). Loaded once from the
 * classpath resource `works/squirrel/suica/StationCode.csv`.
 */
object StationDb {
    data class Station(
        val company: String,
        val lineName: String,
        val stationName: String,
        val note: String = ""
    ) {
        /** Short label for history rows, e.g. "乃木坂". */
        fun shortLabel(): String = stationName.ifBlank { "？" }

        /** Longer label with line (and optional note) when useful. */
        fun displayLabel(withNote: Boolean = false): String {
            if (stationName.isBlank()) return "？"
            val base = if (lineName.isBlank()) stationName else "$stationName（$lineName）"
            return if (withNote && note.isNotBlank()) "$base [$note]" else base
        }
    }

    private val byKey: Map<Long, Station> by lazy { load() }

    fun size(): Int = byKey.size

    fun lookup(area: Int, line: Int, station: Int): Station? {
        if (line == 0 && station == 0) return null
        return byKey[pack(area and 0xff, line and 0xff, station and 0xff)]
    }

    /**
     * Resolve a rail station using the history region byte and line/station codes.
     * JR lines (line &lt; 0x80) always use area 0. Private railways use the 2-bit
     * region field ([forExit] chooses bits 5–4 vs 7–6).
     */
    fun resolveRail(regionByte: Int, line: Int, station: Int, forExit: Boolean): Station? {
        if (line == 0 && station == 0) return null
        val area = areaCode(regionByte, line, forExit)
        lookup(area, line, station)?.let { return it }
        // Fallback: many Kanto private codes live under area 0 even when bits differ.
        if (area != 0) lookup(0, line, station)?.let { return it }
        return null
    }

    fun areaCode(regionByte: Int, line: Int, forExit: Boolean): Int {
        if ((line and 0xff) < 0x80) return 0
        val shift = if (forExit) 4 else 6
        return (regionByte shr shift) and 0x3
    }

    fun formatCode(line: Int, station: Int): String =
        "%02X/%02X".format(line and 0xff, station and 0xff)

    private fun pack(area: Int, line: Int, station: Int): Long =
        ((area.toLong() and 0xff) shl 16) or
            ((line.toLong() and 0xff) shl 8) or
            (station.toLong() and 0xff)

    private fun load(): Map<Long, Station> {
        val stream = StationDb::class.java.getResourceAsStream(
            "/works/squirrel/suica/StationCode.csv"
        ) ?: return emptyMap()
        val out = HashMap<Long, Station>(8192)
        stream.bufferedReader(Charsets.UTF_8).useLines { lines ->
            lines.drop(1).forEach { line ->
                if (line.isBlank()) return@forEach
                val parts = line.split(',')
                if (parts.size < 6) return@forEach
                val area = parts[0].trim().toIntOrNull(16) ?: return@forEach
                val lineCode = parts[1].trim().toIntOrNull(16) ?: return@forEach
                val station = parts[2].trim().toIntOrNull(16) ?: return@forEach
                val company = parts[3].trim()
                val lineName = parts[4].trim()
                val stationName = parts[5].trim()
                val note = if (parts.size > 6) parts[6].trim() else ""
                out.putIfAbsent(
                    pack(area, lineCode, station),
                    Station(company, lineName, stationName, note)
                )
            }
        }
        return out
    }
}

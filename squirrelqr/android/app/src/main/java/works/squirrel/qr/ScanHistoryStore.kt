package works.squirrel.qr

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class ScanEntry(
    val text: String,
    val format: String,
    val timestampMs: Long
)

/** Persistent newest-first scan history (capped). */
class ScanHistoryStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun load(): MutableList<ScanEntry> {
        val raw = prefs.getString(KEY, "[]") ?: "[]"
        return try {
            val arr = JSONArray(raw)
            val out = ArrayList<ScanEntry>(arr.length())
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                out.add(
                    ScanEntry(
                        text = o.getString("text"),
                        format = o.optString("format", "QR"),
                        timestampMs = o.optLong("ts", 0L)
                    )
                )
            }
            out
        } catch (_: Exception) {
            mutableListOf()
        }
    }

    fun save(entries: List<ScanEntry>) {
        val arr = JSONArray()
        for (e in entries.take(MAX)) {
            arr.put(
                JSONObject()
                    .put("text", e.text)
                    .put("format", e.format)
                    .put("ts", e.timestampMs)
            )
        }
        prefs.edit().putString(KEY, arr.toString()).apply()
    }

    fun clear() {
        prefs.edit().putString(KEY, "[]").apply()
    }

    companion object {
        private const val PREFS = "squirrel_qr_history"
        private const val KEY = "entries"
        const val MAX = 40
    }
}

/**
 * In-memory batch of unique scans (de-duplicated by content text).
 * Pure Kotlin so JVM unit tests cover acceptance without CameraX.
 */
class BatchScanSession {
    private val seen = LinkedHashSet<String>()
    private val items = ArrayList<ScanEntry>()

    val size: Int get() = items.size
    val isEmpty: Boolean get() = items.isEmpty()

    fun entries(): List<ScanEntry> = items.toList()

    /** Adds [entry] if its text is new; returns true when accepted. */
    fun tryAdd(entry: ScanEntry): Boolean {
        val key = entry.text
        if (key.isBlank() || !seen.add(key)) return false
        items.add(entry)
        return true
    }

    fun clear() {
        seen.clear()
        items.clear()
    }

    /** One scan per line: `format\\ttext`. */
    fun exportText(): String = buildString {
        for (e in items) {
            append(e.format)
            append('\t')
            append(e.text)
            append('\n')
        }
    }

    /** CSV with header `format,timestamp_ms,text` (text fields quoted). */
    fun exportCsv(): String = buildString {
        append("format,timestamp_ms,text\n")
        for (e in items) {
            append(csvField(e.format))
            append(',')
            append(e.timestampMs)
            append(',')
            append(csvField(e.text))
            append('\n')
        }
    }

    companion object {
        fun csvField(value: String): String {
            val needsQuotes = value.indexOfAny(charArrayOf(',', '"', '\n', '\r')) >= 0
            if (!needsQuotes) return value
            return buildString {
                append('"')
                for (c in value) {
                    if (c == '"') append('"')
                    append(c)
                }
                append('"')
            }
        }
    }
}

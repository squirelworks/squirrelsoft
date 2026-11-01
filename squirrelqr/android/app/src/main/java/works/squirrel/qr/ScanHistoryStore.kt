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

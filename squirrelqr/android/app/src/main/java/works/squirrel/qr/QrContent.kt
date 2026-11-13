package works.squirrel.qr

import java.net.URI
import java.util.Locale

object QrContent {
    /** True when [text] looks like an http(s) URL the system browser can open. */
    fun isHttpUrl(text: String): Boolean {
        val t = text.trim()
        if (t.isEmpty()) return false
        val uri = try {
            URI(t)
        } catch (_: Exception) {
            return false
        }
        val scheme = uri.scheme?.lowercase(Locale.US) ?: return false
        if (scheme != "http" && scheme != "https") return false
        val host = uri.host
        return !host.isNullOrBlank()
    }

    fun displayLabel(text: String, maxLen: Int = 120): String {
        val t = text.trim().replace('\n', ' ')
        return if (t.length <= maxLen) t else t.take(maxLen - 1) + "…"
    }

    /**
     * The fields of a structured payload as lines under its type, the same
     * shape the desktop window shows ("[Wi-Fi network]", "Network: Nest", ...).
     * Null for plain text, which needs nothing beyond the text itself.
     */
    fun details(content: SqQr.Content?): String? {
        if (content == null || content.isPlainText) return null
        val sb = StringBuilder("[").append(content.typeName).append(']')
        for ((label, value) in content.fields) {
            sb.append('\n').append(label).append(": ").append(value.replace('\n', ' '))
        }
        return sb.toString()
    }

    /** squirrelqr-YYYYMMDD-HHMMSS.png in local time, the desktop app's name for saved codes. */
    fun madeFileName(nowMs: Long): String =
        "squirrelqr-" + java.text.SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(java.util.Date(nowMs)) + ".png"

    /** Short words for a level choice, for the Make dialog. */
    fun levelLabel(level: SqQr.Level): String = when (level) {
        SqQr.Level.L -> "L (7%)"
        SqQr.Level.M -> "M (15%)"
        SqQr.Level.Q -> "Q (25%)"
        SqQr.Level.H -> "H (30%)"
    }
}

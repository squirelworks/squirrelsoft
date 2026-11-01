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
}

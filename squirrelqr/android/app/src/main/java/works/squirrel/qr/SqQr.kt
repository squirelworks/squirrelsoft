package works.squirrel.qr

import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.Charset
import java.nio.charset.CodingErrorAction

/**
 * Kotlin side of the JNI bridge to Squirrel's own QR decoder (libs/sqqr).
 *
 * ML Kit stays the first decoder; this one looks at frames where ML Kit found
 * nothing (for example light-on-dark codes) and is the same code the desktop
 * apps use. It also makes codes (the same encoder as `squirrelqr --encode`)
 * and reads what a payload means (Wi-Fi, contact, e-mail, ...).
 * src/main/cpp/sqqr_jni.c fills the meta and info arrays; keep them in step.
 */
object SqQr {
    private const val META_LEN = 12
    private const val INFO_LEN = 5

    /** Error correction levels, in the order libs/sqqr numbers them. */
    enum class Level { L, M, Q, H }

    /** A made symbol: [size] x [size] modules row by row, 1 = dark. */
    class Symbol(val size: Int, val modules: ByteArray, val version: Int, val mask: Int, val mode: Int) {
        fun dark(row: Int, col: Int): Boolean = modules[row * size + col].toInt() != 0
    }

    /** What [encode] gives back: a symbol, or why there is none. */
    sealed interface Made {
        class Ok(val symbol: Symbol) : Made
        /** [code] is libs/sqqr's SQQR_ERR_*; [TOO_LONG] when the text does not fit. */
        class Failed(val code: Int) : Made
    }

    /** SQQR_ERR_OVERFLOW: the text does not fit in version 40 at the level. */
    const val TOO_LONG = -6
    /** No native core (JVM unit tests). */
    const val NO_CORE = -100

    /** What a payload means: type id ("wifi", "url", ...), its name, and labelled fields. */
    class Content(val typeId: String, val typeName: String, val fields: List<Pair<String, String>>) {
        fun get(label: String): String? = fields.firstOrNull { it.first == label }?.second
        val isPlainText: Boolean get() = typeId == "text"
    }

    /** A decoded symbol: text, and its corners in the unrotated buffer (x,y pairs TL, TR, BR, BL). */
    class Hit(val text: String, val corners: FloatArray, val version: Int)

    /** False when the native library is missing (e.g. JVM unit tests). */
    val available: Boolean by lazy {
        try {
            System.loadLibrary("sqqr_jni")
            true
        } catch (_: UnsatisfiedLinkError) {
            false
        }
    }

    @JvmStatic
    private external fun nativeDecode(gray: ByteArray, width: Int, height: Int, stride: Int, meta: DoubleArray): ByteArray?

    @JvmStatic
    private external fun nativeEncode(data: ByteArray, level: Int, info: IntArray): ByteArray?

    @JvmStatic
    private external fun nativeContent(utf8: ByteArray): ByteArray?

    @JvmStatic
    private external fun nativeWifi(ssid: ByteArray, password: ByteArray, security: ByteArray, hidden: Boolean): ByteArray?

    /** Makes a QR code for [text] (UTF-8; automatic version, mode and mask). */
    fun encode(text: String, level: Level = Level.M): Made {
        if (!available) return Made.Failed(NO_CORE)
        val info = IntArray(INFO_LEN)
        val grid = nativeEncode(text.toByteArray(Charsets.UTF_8), level.ordinal, info)
            ?: return Made.Failed(if (info[0] != 0) info[0] else NO_CORE)
        return Made.Ok(Symbol(info[2], grid, info[1], info[3], info[4]))
    }

    /** What [text] means, or null without the native core. */
    fun content(text: String): Content? {
        if (!available) return null
        val raw = nativeContent(text.toByteArray(Charsets.UTF_8)) ?: return null
        return contentOf(raw)
    }

    /** A WIFI: payload; [security] is "WPA", "WEP", "SAE", "nopass" or "" (pick from the password). */
    fun wifi(ssid: String, password: String, security: String = "", hidden: Boolean = false): String? {
        if (!available) return null
        val out = nativeWifi(
            ssid.toByteArray(Charsets.UTF_8),
            password.toByteArray(Charsets.UTF_8),
            security.toByteArray(Charsets.UTF_8),
            hidden,
        ) ?: return null
        return String(out, Charsets.UTF_8)
    }

    /** nativeContent's bytes (NUL-ended UTF-8: id, name, then label/value pairs) to a [Content]. */
    fun contentOf(raw: ByteArray): Content? {
        val parts = ArrayList<String>()
        var start = 0
        for (i in raw.indices) {
            if (raw[i].toInt() == 0) {
                parts.add(String(raw, start, i - start, Charsets.UTF_8))
                start = i + 1
            }
        }
        if (parts.size < 2) return null
        val fields = ArrayList<Pair<String, String>>()
        var k = 2
        while (k + 1 < parts.size) {
            fields.add(parts[k] to parts[k + 1])
            k += 2
        }
        return Content(parts[0], parts[1], fields)
    }

    /** Decodes one QR code in an 8-bit luminance image, or returns null (none found, or no native core). */
    fun decode(gray: ByteArray, width: Int, height: Int, stride: Int = width): Hit? {
        if (!available) return null
        val meta = DoubleArray(META_LEN)
        val bytes = nativeDecode(gray, width, height, stride, meta) ?: return null
        val corners = FloatArray(8) { meta[it].toFloat() }
        return Hit(text(bytes, meta[8].toInt(), meta[9] != 0.0), corners, meta[10].toInt())
    }

    /**
     * Payload bytes to text. An ECI names the charset; without one, Kanji mode
     * means Shift JIS. Otherwise the bytes are tried as UTF-8 (what most
     * generators write), then Shift JIS (Japanese generators often use it in
     * byte mode without an ECI), then the standard's ISO-8859-1 default.
     */
    fun text(bytes: ByteArray, eci: Int, hasKanji: Boolean): String {
        val named = when (eci) {
            1, 3 -> "ISO-8859-1"
            20 -> "Shift_JIS"
            26 -> "UTF-8"
            else -> null
        }
        if (named != null) return String(bytes, Charset.forName(named))
        if (hasKanji) return String(bytes, Charset.forName("Shift_JIS"))
        return strict(bytes, Charsets.UTF_8)
            ?: strict(bytes, Charset.forName("Shift_JIS"))
            ?: String(bytes, Charsets.ISO_8859_1)
    }

    private fun strict(bytes: ByteArray, cs: Charset): String? = try {
        cs.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(bytes)).toString()
    } catch (_: CharacterCodingException) {
        null
    }
}

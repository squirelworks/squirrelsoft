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
 * apps use. src/main/cpp/sqqr_jni.c fills the meta array; keep them in step.
 */
object SqQr {
    private const val META_LEN = 12

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

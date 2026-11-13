package works.squirrel.qr

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SqQrTest {
    @Test
    fun textUsesUtf8WhenValid() {
        val bytes = "スクワレル squirrel".toByteArray(Charsets.UTF_8)
        assertEquals("スクワレル squirrel", SqQr.text(bytes, -1, false))
    }

    @Test
    fun textFallsBackToLatin1ForInvalidUtf8() {
        val bytes = byteArrayOf(0x63, 0x61, 0x66, 0xE9.toByte()) // "café" in ISO-8859-1
        assertEquals("café", SqQr.text(bytes, -1, false))
    }

    @Test
    fun textReadsShiftJisInByteModeWithoutEci() {
        // What segno writes for katakana plus a URL: Shift JIS bytes, byte mode, no ECI.
        val bytes = "スクワレル https://squirrel.works/".toByteArray(charset("Shift_JIS"))
        assertEquals("スクワレル https://squirrel.works/", SqQr.text(bytes, -1, false))
    }

    @Test
    fun textHonoursEciAndKanji() {
        val sjis = "日本".toByteArray(charset("Shift_JIS"))
        assertEquals("日本", SqQr.text(sjis, -1, true))
        assertEquals("日本", SqQr.text(sjis, 20, false))
        val latin = byteArrayOf(0xC3.toByte(), 0xA9.toByte())
        assertEquals("Ã©", SqQr.text(latin, 3, false)) // ECI 3 forces ISO-8859-1 even if UTF-8 would parse
        assertEquals("é", SqQr.text(latin, 26, false))
    }

    @Test
    fun decodeWithoutNativeCoreIsNull() {
        // JVM unit tests have no libsqqr_jni.so.
        assertFalse(SqQr.available)
        assertNull(SqQr.decode(ByteArray(64 * 64), 64, 64))
    }

    @Test
    fun withoutNativeCoreMakingAndParsingSayNoCore() {
        val made = SqQr.encode("hello")
        assertTrue(made is SqQr.Made.Failed && made.code == SqQr.NO_CORE)
        assertNull(SqQr.content("WIFI:S:x;;"))
        assertNull(SqQr.wifi("x", ""))
    }

    @Test
    fun contentOfSplitsNulEndedUtf8() {
        val raw = "wifi\u0000Wi-Fi network\u0000Network\u0000Nest 🐿\u0000Password\u0000acorns\u0000"
            .toByteArray(Charsets.UTF_8)
        val c = SqQr.contentOf(raw)!!
        assertEquals("wifi", c.typeId)
        assertEquals("Wi-Fi network", c.typeName)
        assertEquals(listOf("Network" to "Nest 🐿", "Password" to "acorns"), c.fields)
        assertEquals("acorns", c.get("Password"))
        assertNull(c.get("Hidden"))
        assertFalse(c.isPlainText)
    }

    @Test
    fun contentOfPlainTextAndJunk() {
        val plain = SqQr.contentOf("text\u0000Text\u0000".toByteArray())!!
        assertTrue(plain.isPlainText)
        assertTrue(plain.fields.isEmpty())
        assertNull(SqQr.contentOf(ByteArray(0)))
        assertNull(SqQr.contentOf("text".toByteArray())) // no terminator
        // a label without its value is dropped
        assertEquals(0, SqQr.contentOf("url\u0000Web link\u0000URL\u0000".toByteArray())!!.fields.size)
    }
}

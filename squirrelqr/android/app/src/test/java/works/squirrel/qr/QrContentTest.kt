package works.squirrel.qr

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class QrContentTest {
    @Test
    fun httpUrlsAccepted() {
        assertTrue(QrContent.isHttpUrl("https://example.com/path"))
        assertTrue(QrContent.isHttpUrl("http://squirrel.works"))
        assertTrue(QrContent.isHttpUrl("  https://a.co  "))
    }

    @Test
    fun nonHttpRejected() {
        assertFalse(QrContent.isHttpUrl("plain text"))
        assertFalse(QrContent.isHttpUrl("WIFI:T:WPA;S:Home;P:secret;;"))
        assertFalse(QrContent.isHttpUrl("ftp://files.example"))
        assertFalse(QrContent.isHttpUrl("https://"))
        assertFalse(QrContent.isHttpUrl(""))
    }

    @Test
    fun displayLabelTruncates() {
        val long = "x".repeat(200)
        val label = QrContent.displayLabel(long, maxLen = 40)
        assertEquals(40, label.length)
        assertTrue(label.endsWith("…"))
    }

    @Test
    fun detailsListFieldsUnderTheType() {
        val wifi = SqQr.Content("wifi", "Wi-Fi network", listOf("Network" to "Nest", "Password" to "acorns"))
        assertEquals("[Wi-Fi network]\nNetwork: Nest\nPassword: acorns", QrContent.details(wifi))
        val note = SqQr.Content("contact", "Contact", listOf("Note" to "two\nlines"))
        assertEquals("[Contact]\nNote: two lines", QrContent.details(note))
    }

    @Test
    fun detailsNullForPlainTextOrNoCore() {
        assertEquals(null, QrContent.details(SqQr.Content("text", "Text", emptyList())))
        assertEquals(null, QrContent.details(null))
    }

    @Test
    fun madeFileNameLooksLikeTheDesktops() {
        val name = QrContent.madeFileName(1_700_000_000_000L)
        assertTrue(name, Regex("squirrelqr-\\d{8}-\\d{6}\\.png").matches(name))
    }

    @Test
    fun levelLabelsNameTheLetter() {
        for (lv in SqQr.Level.values()) assertTrue(QrContent.levelLabel(lv).startsWith(lv.name))
    }
}

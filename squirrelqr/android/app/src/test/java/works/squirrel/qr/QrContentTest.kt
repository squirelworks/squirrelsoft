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
}

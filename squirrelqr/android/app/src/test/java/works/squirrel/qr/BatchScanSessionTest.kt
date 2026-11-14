package works.squirrel.qr

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BatchScanSessionTest {
    @Test
    fun acceptsFirstOccurrenceAndRejectsDuplicatesByContent() {
        val batch = BatchScanSession()
        val a = ScanEntry("https://example.com/a", "QR", 1_000L)
        val aAgain = ScanEntry("https://example.com/a", "QR", 2_000L)
        val b = ScanEntry("WIFI:T:WPA;S:Nest;P:acorns;;", "QR", 3_000L)

        assertTrue(batch.tryAdd(a))
        assertEquals(1, batch.size)
        assertFalse(batch.tryAdd(aAgain))
        assertEquals(1, batch.size)
        assertTrue(batch.tryAdd(b))
        assertEquals(2, batch.size)

        val texts = batch.entries().map { it.text }
        assertEquals(listOf(a.text, b.text), texts)
        // First timestamp wins; duplicate does not replace.
        assertEquals(1_000L, batch.entries()[0].timestampMs)
    }

    @Test
    fun blankContentIsRejected() {
        val batch = BatchScanSession()
        assertFalse(batch.tryAdd(ScanEntry("", "QR", 1L)))
        assertFalse(batch.tryAdd(ScanEntry("   ".trim(), "QR", 1L)))
        assertTrue(batch.isEmpty)
    }

    @Test
    fun clearEmptiesSession() {
        val batch = BatchScanSession()
        assertTrue(batch.tryAdd(ScanEntry("one", "QR", 1L)))
        assertTrue(batch.tryAdd(ScanEntry("two", "QR", 2L)))
        batch.clear()
        assertTrue(batch.isEmpty)
        assertEquals(0, batch.size)
        assertTrue(batch.tryAdd(ScanEntry("one", "QR", 3L)))
        assertEquals(1, batch.size)
    }

    @Test
    fun exportTextIsTabSeparatedLines() {
        val batch = BatchScanSession()
        batch.tryAdd(ScanEntry("alpha", "QR", 10L))
        batch.tryAdd(ScanEntry("beta", "QR", 20L))
        assertEquals("QR\talpha\nQR\tbeta\n", batch.exportText())
    }

    @Test
    fun exportCsvQuotesFieldsWithCommasAndQuotes() {
        val batch = BatchScanSession()
        batch.tryAdd(ScanEntry("plain", "QR", 11L))
        batch.tryAdd(ScanEntry("hello, \"world\"", "QR", 22L))
        val csv = batch.exportCsv()
        assertEquals(
            "format,timestamp_ms,text\n" +
                "QR,11,plain\n" +
                "QR,22,\"hello, \"\"world\"\"\"\n",
            csv
        )
    }

    @Test
    fun csvFieldHelper() {
        assertEquals("ok", BatchScanSession.csvField("ok"))
        assertEquals("\"a,b\"", BatchScanSession.csvField("a,b"))
        assertEquals("\"say \"\"hi\"\"\"", BatchScanSession.csvField("say \"hi\""))
        assertEquals("\"line1\nline2\"", BatchScanSession.csvField("line1\nline2"))
    }
}

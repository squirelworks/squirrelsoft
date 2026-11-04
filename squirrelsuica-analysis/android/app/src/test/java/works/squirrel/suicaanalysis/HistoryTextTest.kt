package works.squirrel.suicaanalysis

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HistoryTextTest {
    private fun hex(vararg b: Int) = b.joinToString("") { "%02x".format(it) }

    private fun service(code: Int, blocks: List<String>, readable: Boolean = true) =
        ServiceDump(code, FelicaProtocol.serviceKind(code), readable, 0, blocks, emptyList())

    private fun dump(vararg services: ServiceDump) = CardDump(
        createdAt = "2026-11-04T00:00:00Z", tool = "test", idm = null, pmm = null,
        polls = emptyList(), systemCodes = listOf(0x0003),
        systems = listOf(SystemDump(0x0003, null, null, null, emptyList(), services.toList(), null)),
        log = emptyList(), error = null
    )

    // Same order as SqFelica's FIELDS doc and sqfelica_jni.c.
    private fun flat(vararg rows: IntArray) = rows.fold(IntArray(0)) { a, r -> a + r }

    @Test
    fun decodeReadsEveryField() {
        val row = intArrayOf(0, 2026, 10, 28, -1, -1, 168, 2480, 0, 0x16, 0x01, 1, 0xE3, 0x59, 1, 0xE3, 0x5E, 0)
        val e = SqFelica.decode(flat(row)).single()
        assertEquals("train", e.kind)
        assertEquals(Triple(2026, 10, 28), Triple(e.year, e.month, e.day))
        assertEquals(-1, e.hour)
        assertEquals(168, e.fareYen)
        assertEquals(2480, e.balanceYen)
        assertEquals(0xE3 to 0x59, e.enter)
        assertEquals(0xE3 to 0x5E, e.exit)
        assertEquals(3, SqFelica.Brand.WAON.id)
    }

    @Test
    fun decodeHandlesMissingStationsAndUnknownKind() {
        val row = intArrayOf(9, 0, 0, 0, 12, 5, 0, 500, 3000, 0x6f, 0x6f, 0, 0, 0, 0, 0, 0, 0)
        val e = SqFelica.decode(row).single()
        assertEquals("other", e.kind)
        assertEquals(null, e.enter)
        assertEquals(null, e.exit)
        assertEquals(0, SqFelica.decode(IntArray(0)).size)
    }

    @Test(expected = IllegalArgumentException::class)
    fun decodeRejectsTruncatedArray() {
        SqFelica.decode(IntArray(SqFelica.FIELDS - 1))
    }

    @Test
    fun lineFormatsRailChargeAndTimedEntries() {
        val rail = SqFelica.decode(intArrayOf(0, 2026, 10, 28, -1, -1, 168, 2480, 0, 0x16, 0x01, 1, 0xE3, 0x59, 1, 0xE3, 0x5E, 0)).single()
        assertEquals("2026-10-28       train     -¥168  bal ¥2,480  t16 p01  E3/59 → E3/5E", HistoryText.line(rail))
        val charge = rail.copy(kind = "charge", fareYen = 2000, exit = null)
        assertTrue(HistoryText.line(charge).contains("+¥2,000"))
        val timed = rail.copy(year = 0, hour = 18, minute = 42, enter = null, exit = null, fareYen = 0)
        assertEquals("????-??-?? 18:42 train            bal ¥2,480  t16 p01", HistoryText.line(timed))
    }

    @Test
    fun renderPassesSuicaBlocksAndBalanceBlocksToTheCore() {
        val b0 = hex(0x16, 0x01, 0, 0, 0x35, 0x5c, 0xe3, 0x59, 0xe3, 0x5e, 0xb0, 0x09, 0, 0, 0, 0)
        val info = hex(*IntArray(16))
        val d = dump(service(0x090f, listOf(b0, b0)), service(0x008b, listOf(info)), service(0x1008, listOf(b0), readable = false))
        var gotBlocks = 0
        var gotBalance: Pair<ByteArray?, ByteArray?>? = null
        val text = HistoryText.render(
            d,
            parser = { brand, blocks ->
                assertEquals(SqFelica.Brand.SUICA, brand)
                gotBlocks = blocks.size
                SqFelica.decode(intArrayOf(0, 2026, 10, 28, -1, -1, 168, 2480, 0, 0x16, 0x01, 1, 0xE3, 0x59, 1, 0xE3, 0x5E, 0))
            },
            balancer = { brand, a, b -> gotBalance = a to b; if (brand == SqFelica.Brand.SUICA) 2480 else -1 }
        )
        assertEquals(2, gotBlocks)
        assertEquals(0xb0, gotBalance!!.first!![10].toInt() and 0xff)
        assertEquals(16, gotBalance!!.second!!.size)
        assertTrue(text, text.contains("== Suica / PASMO (svc 090f)"))
        assertTrue(text, text.contains("Balance ¥2,480"))
        assertTrue(text, text.contains("00 2026-10-28"))
        assertTrue(text, !text.contains("nanaco"))
    }

    @Test
    fun renderSaysWhenNothingIsReadableOrNativeCoreIsMissing() {
        assertTrue(HistoryText.render(dump(), { _, _ -> null }, { _, _, _ -> null }).contains("No Suica, nanaco, Edy or WAON"))
        val d = dump(service(0x170f, listOf(hex(*IntArray(16)))))
        val text = HistoryText.render(d, { _, _ -> null }, { _, _, _ -> null })
        assertTrue(text, text.contains("== Edy (svc 170f)"))
        assertTrue(text, text.contains("parser not available"))
    }
}

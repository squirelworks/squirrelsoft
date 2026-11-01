package works.squirrel.suica

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Fixture tests — no physical cards required.
 * Layouts follow publicly documented open-reader notes.
 */
class FelicaParseTest {
    @Test
    fun suica090fBalance() {
        val block = byteArrayOf(0xB0.toByte(), 0x09) + ByteArray(14)
        assertEquals(2480, FelicaParse.suicaBalance090f(block))
    }

    @Test
    fun suica008bPreferredWhen090fIsRoughlyTenTimesLarger() {
        // Matches the field report: UI showed ¥50945 while true balance ~¥5094.
        val inflated090f = byteArrayOf(0xE1.toByte(), 0xC6.toByte()) + ByteArray(14) // 50945
        val info008b = ByteArray(16)
        info008b[11] = 0xE6.toByte()
        info008b[12] = 0x13 // 0x13E6 = 5094
        assertEquals(5094, FelicaParse.suicaBalance(inflated090f, info008b))
        assertEquals(5094, FelicaParse.suicaBalance008b(info008b))
    }

    @Test
    fun nanacoBalanceLe32() {
        val block = byteArrayOf(0x50, 0x14, 0x00, 0x00) + ByteArray(12) // 5200
        assertEquals(5200, FelicaParse.nanacoBalance(block))
    }

    @Test
    fun edyBalanceLe32() {
        val block = byteArrayOf(0x1C, 0x0C, 0x00, 0x00) + ByteArray(12)
        assertEquals(3100, FelicaParse.edyBalance(block))
    }

    @Test
    fun waonBalanceLe16() {
        val block = byteArrayOf(0xE8.toByte(), 0x11) + ByteArray(14)
        assertEquals(4584, FelicaParse.waonBalance(block))
    }

    @Test
    fun demosAreRealisticNotInflated() {
        assertTrue(FelicaParse.demoSuica().balanceYen!! < 10_000)
        assertTrue(FelicaParse.demoNanaco().balanceYen!! < 10_000)
        assertTrue(FelicaParse.demoEdy().balanceYen!! < 10_000)
        assertTrue(FelicaParse.demoWaon().balanceYen!! < 10_000)
        assertEquals(FelicaBrand.NANACO, FelicaParse.demoNanaco().brand)
    }

    @Test
    fun extractBlocksRejectsGarbage() {
        val junk = ByteArray(32) { it.toByte() }
        assertTrue(FelicaReader.extractBlocks(junk).isEmpty())
    }

    @Test
    fun extractBlocksValidResponse() {
        val idm = ByteArray(8) { it.toByte() }
        val block = ByteArray(16)
        block[0] = 0xB0.toByte()
        block[1] = 0x09
        val body = ArrayList<Byte>()
        body.add(0x07)
        idm.forEach { body.add(it) }
        body.add(0); body.add(0)
        block.forEach { body.add(it) }
        val packet = byteArrayOf((body.size + 1).toByte()) + body.toByteArray()
        val got = FelicaReader.extractBlocks(packet)
        assertEquals(1, got.size)
        assertEquals(2480, FelicaParse.suicaBalance090f(got[0]))
    }

    @Test
    fun emptyHistory() {
        assertNull(FelicaParse.historyEntry(ByteArray(16)))
    }

    @Test
    fun stationDbLoadsCommunityTable() {
        assertTrue(StationDb.size() > 1000)
        val nogi = StationDb.lookup(0, 0xe3, 0x59)
        assertNotNull(nogi)
        assertEquals("乃木坂", nogi!!.stationName)
        val yoyogi = StationDb.lookup(0, 0xe3, 0x5e)
        assertEquals("代々木上原", yoyogi!!.stationName)
        val tokyo = StationDb.lookup(0, 0x01, 0x01)
        assertEquals("東京", tokyo!!.stationName)
    }

    @Test
    fun qiitaNogizakaToYoyogiUeharaFixture() {
        // Public sample from Qiita open write-up (乃木坂→代々木上原, bal ¥2929, 2018-04-25).
        val hex = "160100042499e359e35e710b001b0800"
        val block = hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
        val e = FelicaParse.historyEntry(block)!!
        assertEquals("2018-04-25", e.date)
        assertEquals("train", e.kind)
        assertEquals(2929, e.balanceYen)
        assertEquals("乃木坂", e.enterStation)
        assertEquals("代々木上原", e.exitStation)
        assertEquals("E3/59", e.enterCode)
        assertEquals("E3/5E", e.exitCode)
        assertTrue(e.stationSummary().contains("乃木坂"))
        assertTrue(e.stationSummary().contains("代々木上原"))
        assertTrue(e.note.contains("乃木坂"))
    }

    @Test
    fun demoSuicaResolvesStationNamesAndFares() {
        val card = FelicaParse.demoSuica()
        assertTrue(card.history.size >= 3)
        val ride = card.history[0]
        assertEquals("乃木坂", ride.enterStation)
        assertEquals("代々木上原", ride.exitStation)
        assertEquals(168, ride.fareYen) // 2648 - 2480
        val yamanote = card.history[1]
        assertEquals("渋谷", yamanote.enterStation)
        assertEquals("新宿", yamanote.exitStation)
        val charge = card.history[2]
        assertEquals("charge", charge.kind)
        assertEquals("東京", charge.enterStation)
        assertNull(charge.exitStation)
    }

    @Test
    fun unknownStationFallsBackToCode() {
        val block = FelicaParse.histRail(
            terminal = 0x16,
            process = 0x01,
            y = 2026, m = 1, d = 2,
            inLine = 0xff, inSta = 0xfe,
            outLine = 0xff, outSta = 0xfd,
            bal = 1000,
            region = 0x00
        )
        val e = FelicaParse.historyEntry(block)!!
        assertEquals("FF/FE", e.enterStation)
        assertEquals("FF/FD", e.exitStation)
        assertFalse(e.stationSummary().isEmpty())
    }
}

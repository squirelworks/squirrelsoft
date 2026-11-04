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
    fun suica090fBalanceAtOffset10() {
        // Balance is at bytes 10–11 LE (not 0–1 = terminal/process).
        val block = FelicaParse.histRail(
            terminal = 0x16,
            process = 0x01,
            y = 2026, m = 10, d = 28,
            inLine = 0xe3, inSta = 0x59,
            outLine = 0xe3, outSta = 0x5e,
            bal = 2480,
            region = 0
        )
        assertEquals(2480, FelicaParse.suicaBalance090f(block))
        // Bytes 0–1 are NOT the balance (would be 0x0116 = 278).
        assertEquals(0x16, block[0].toInt() and 0xff)
        assertEquals(0x01, block[1].toInt() and 0xff)
    }

    @Test
    fun suica008bPreferredWhen090fIsRoughlyTenTimesLarger() {
        // Guard for legacy mis-parses / corrupted history vs card-info.
        val hist = FelicaParse.histRail(
            terminal = 0x16, process = 0x01,
            y = 2026, m = 1, d = 1,
            inLine = 1, inSta = 1, outLine = 1, outSta = 2,
            bal = 50945, region = 0
        )
        val info008b = ByteArray(16)
        info008b[11] = 0xE6.toByte()
        info008b[12] = 0x13 // 0x13E6 = 5094
        assertEquals(5094, FelicaParse.suicaBalance(hist, info008b))
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
    fun fixtureBalancesAreRealisticNotInflated() {
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
        val block = FelicaParse.histRail(
            terminal = 0x16, process = 0x01,
            y = 2026, m = 10, d = 28,
            inLine = 0xe3, inSta = 0x59, outLine = 0xe3, outSta = 0x5e,
            bal = 2480, region = 0
        )
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
        assertEquals(2929, FelicaParse.suicaBalance090f(block))
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
        assertEquals(HistoryStatus.OK, card.historyStatus)
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
        assertEquals(2000, charge.fareYen) // 2858 - 858
    }

    @Test
    fun historyFormatShowsStationsFareBalance() {
        val card = FelicaParse.demoSuica()
        val text = HistoryFormat.formatHistoryPanel(card)
        assertTrue(text.contains("乃木坂"))
        assertTrue(text.contains("代々木上原"))
        assertTrue(text.contains("残高"))
        assertTrue(text.contains("運賃"))
        assertFalse(text.contains("(empty)"))
        assertFalse(text.contains("(no history blocks)"))
    }

    @Test
    fun historyFormatReadFailedIsActionable() {
        val text = HistoryFormat.emptyMessage(HistoryStatus.READ_FAILED)
        assertTrue(text.contains("Could not read history"))
        assertTrue(text.contains("Hold the card still"))
        assertFalse(text.contains("(empty)"))
    }

    @Test
    fun multiBlockReadCommandEncodesFourIndices() {
        val idm = ByteArray(8) { (it + 1).toByte() }
        val cmd = FelicaReader.readWithoutEncryption(idm, 0x090f, intArrayOf(0, 1, 2, 3))
        assertEquals(4, cmd[13].toInt() and 0xff)
        val elements = cmd.copyOfRange(14, 22).map { it.toInt() and 0xff }
        assertEquals(listOf(0x80, 0x00, 0x80, 0x01, 0x80, 0x02, 0x80, 0x03), elements)
        assertEquals(cmd.size, cmd[0].toInt() and 0xff)
    }

    @Test
    fun blockListElementsKeepServiceOrderZeroForBlocks0To19() {
        for (b in 0 until 20) {
            val e = FelicaReader.blockListElement(b).map { it.toInt() and 0xff }
            assertEquals("block $b", listOf(0x80, b), e)
        }
    }

    @Test
    fun blockListElementUsesThreeByteFormAbove255() {
        val e = FelicaReader.blockListElement(0x012c).map { it.toInt() and 0xff }
        assertEquals(listOf(0x00, 0x2c, 0x01), e)
        val cmd = FelicaReader.readWithoutEncryption(ByteArray(8), 0x090f, intArrayOf(255, 300))
        assertEquals(listOf(0x80, 0xff, 0x00, 0x2c, 0x01), cmd.copyOfRange(14, 19).map { it.toInt() and 0xff })
        assertEquals(cmd.size, cmd[0].toInt() and 0xff)
    }

    @Test
    fun statusFlagsAreReported() {
        val resp = byteArrayOf(12, 0x07, 1, 2, 3, 4, 5, 6, 7, 8, 0x01, 0xA8.toByte())
        assertEquals(0x01 to 0xA8, FelicaReader.statusFlags(resp))
        assertTrue(FelicaReader.extractBlocks(resp).isEmpty())
    }

    @Test
    fun fakeCardReturnsAllTwentyHistoryBlocks() {
        val card = FakeFelicaCard(blockCount = 20, maxBlocksPerRead = 4)
        val read = FelicaReader.readServiceRange(card, ByteArray(8) { 1 }, 0x090f, 0, 20)
        assertEquals(HistoryStatus.OK, read.status)
        assertEquals(20, read.blocks.size)
        for (i in 0 until 20) assertEquals(i, read.blocks[i][0].toInt() and 0xff)
    }

    @Test
    fun fakeCardWithOneBlockPerReadStillReturnsTwenty() {
        val card = FakeFelicaCard(blockCount = 20, maxBlocksPerRead = 1)
        val read = FelicaReader.readServiceRange(card, ByteArray(8) { 1 }, 0x090f, 0, 20)
        assertEquals(HistoryStatus.OK, read.status)
        assertEquals(20, read.blocks.size)
    }

    @Test
    fun shortServiceEndsCleanlyOnCardRefusal() {
        val card = FakeFelicaCard(blockCount = 6, maxBlocksPerRead = 4)
        val read = FelicaReader.readServiceRange(card, ByteArray(8) { 1 }, 0x090f, 0, 20)
        assertEquals(HistoryStatus.OK, read.status)
        assertEquals(6, read.blocks.size)
    }

    /**
     * Strict fake card: refuses (SF1=01, SF2=A1/A8) any element whose service-list
     * order is not 0, any block past [blockCount], or more than [maxBlocksPerRead]
     * blocks per command. Block i's first byte is i.
     */
    private class FakeFelicaCard(
        private val blockCount: Int,
        private val maxBlocksPerRead: Int
    ) : FelicaReader.Transceiver {
        override fun transceive(command: ByteArray): ByteArray {
            val c = command.map { it.toInt() and 0xff }
            require(c[0] == c.size && c[1] == 0x06)
            val idm = command.copyOfRange(2, 10)
            val nSvc = c[10]
            var off = 11 + 2 * nSvc
            val nBlk = c[off]
            off += 1
            val wanted = ArrayList<Int>()
            var sf2 = 0
            repeat(nBlk) {
                val d0 = c[off]
                if (d0 and 0x80 != 0) {
                    if (d0 and 0x0f != 0) sf2 = 0xA1
                    wanted.add(c[off + 1])
                    off += 2
                } else {
                    if (d0 and 0x0f != 0) sf2 = 0xA1
                    wanted.add(c[off + 1] or (c[off + 2] shl 8))
                    off += 3
                }
            }
            if (sf2 == 0 && nBlk > maxBlocksPerRead) sf2 = 0xA2
            if (sf2 == 0 && wanted.any { it >= blockCount }) sf2 = 0xA8
            val body = ArrayList<Byte>()
            body.add(0x07)
            idm.forEach { body.add(it) }
            if (sf2 != 0) {
                body.add(0x01); body.add(sf2.toByte())
            } else {
                body.add(0x00); body.add(0x00); body.add(wanted.size.toByte())
                for (b in wanted) {
                    body.add(b.toByte())
                    repeat(15) { body.add(0) }
                }
            }
            return (listOf((body.size + 1).toByte()) + body).toByteArray()
        }
    }

    @Test
    fun extractBlocksMultiBlockResponse() {
        val idm = ByteArray(8) { it.toByte() }
        val b0 = FelicaParse.histRail(
            terminal = 0x16, process = 0x01,
            y = 2026, m = 10, d = 28,
            inLine = 0xe3, inSta = 0x59, outLine = 0xe3, outSta = 0x5e,
            bal = 2480, region = 0
        )
        val b1 = FelicaParse.histRail(
            terminal = 0x16, process = 0x01,
            y = 2026, m = 10, d = 27,
            inLine = 0x25, inSta = 0x07, outLine = 0x25, outSta = 0x0a,
            bal = 2648, region = 0
        )
        val body = ArrayList<Byte>()
        body.add(0x07)
        idm.forEach { body.add(it) }
        body.add(0); body.add(0)
        body.add(2)
        b0.forEach { body.add(it) }
        b1.forEach { body.add(it) }
        val packet = byteArrayOf((body.size + 1).toByte()) + body.toByteArray()
        val got = FelicaReader.extractBlocks(packet)
        assertEquals(2, got.size)
        val entries = FelicaParse.historyEntries(got)
        assertEquals(2, entries.size)
        assertEquals("乃木坂", entries[0].enterStation)
        assertEquals("渋谷", entries[1].enterStation)
        assertEquals(168, entries[0].fareYen)
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

    @Test
    fun nanacoHistoryPaymentAndCharge() {
        val pay = FelicaParse.histNanaco(
            type = 0x47, amount = 480, bal = 5200,
            y = 2026, m = 10, d = 28, hh = 18, mm = 42
        )
        val e = FelicaParse.nanacoHistoryEntry(pay)!!
        assertEquals("sale", e.kind)
        assertEquals(480, e.fareYen)
        assertEquals(5200, e.balanceYen)
        assertEquals("2026-10-28", e.date)
        assertTrue(e.note.contains("18:42"))

        val charge = FelicaParse.histNanaco(
            type = 0x6f, amount = 3000, bal = 5680,
            y = 2026, m = 10, d = 27, hh = 12, mm = 5
        )
        val c = FelicaParse.nanacoHistoryEntry(charge)!!
        assertEquals("charge", c.kind)
        assertEquals(5680, c.balanceYen)
        assertTrue(FelicaParse.demoNanaco().history.size >= 2)
    }

    @Test
    fun edyHistoryPayment() {
        val block = FelicaParse.histEdy(
            type = 0x20, amount = 350, bal = 3100,
            days = 0, // 2000-01-01
            seconds = 14 * 3600 + 22 * 60
        )
        val e = FelicaParse.edyHistoryEntry(block)!!
        assertEquals("sale", e.kind)
        assertEquals(350, e.fareYen)
        assertEquals(3100, e.balanceYen)
        assertEquals("2000-01-01", e.date)
        assertTrue(e.note.contains("14:22"))
        assertTrue(FelicaParse.demoEdy().history.isNotEmpty())
    }

    @Test
    fun waonHistoryPayment() {
        val rec = FelicaParse.histWaon(
            type = 0x04,
            y = 2026, m = 10, d = 28, hh = 19, mm = 10,
            bal = 4584, usage = 620, charge = 0
        )
        val e = FelicaParse.waonHistoryEntry(rec)!!
        assertEquals("sale", e.kind)
        assertEquals(620, e.fareYen)
        assertEquals(4584, e.balanceYen)
        assertEquals("2026-10-28", e.date)
        assertTrue(e.note.contains("19:10"))
        assertTrue(FelicaParse.demoWaon().history.isNotEmpty())
    }

    @Test
    fun suicaHistoryServiceCodeIs090fNot010f() {
        // Regression: FelicaReader must read history from 0x090f.
        // Command builder is public — ensure wire encoding for 0x090f.
        val idm = ByteArray(8) { (it + 1).toByte() }
        val cmd = FelicaReader.readWithoutEncryption(idm, 0x090f, intArrayOf(0, 1, 2, 3))
        assertEquals(0x06, cmd[1].toInt() and 0xff)
        // layout: len, cmd, IDm[8], serviceCount, serviceLo, serviceHi, blockCount, …
        assertEquals(0x01, cmd[10].toInt() and 0xff) // one service
        assertEquals(0x0f, cmd[11].toInt() and 0xff) // 0x090f LE
        assertEquals(0x09, cmd[12].toInt() and 0xff)
        assertEquals(4, cmd[13].toInt() and 0xff) // 4 blocks
    }
}

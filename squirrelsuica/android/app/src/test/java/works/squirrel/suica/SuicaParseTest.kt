package works.squirrel.suica

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SuicaParseTest {
    @Test
    fun balanceLittleEndian() {
        // History-block balance is LE u16 at bytes 10–11.
        val block = FelicaParse.histRail(
            terminal = 0x16, process = 0x01,
            y = 2026, m = 10, d = 28,
            inLine = 0xe3, inSta = 0x59, outLine = 0xe3, outSta = 0x5e,
            bal = 2480, region = 0
        )
        assertEquals(2480, SuicaParse.balanceYen(block))
    }

    @Test
    fun demoCard() {
        val card = SuicaParse.demoCard()
        assertEquals(2480, card.balanceYen)
        assertEquals(16, card.idm.length)
        assertTrue(card.history.isNotEmpty())
        assertNotNull(card.history.first().date)
        assertEquals("乃木坂", card.history.first().enterStation)
    }

    @Test
    fun shopSaleOnTerminalCaHasTimeNotStations() {
        // S1.3: a synthetic sale on terminal type CA (process 46), the layout real cards use;
        // bytes 6-7 are the time and 8-9 a terminal ID, not stations.
        val block = "ca460000326470a02a5cd00700012c00".chunked(2).map { it.toInt(16).toByte() }.toByteArray()
        val e = SuicaParse.historyEntry(block)
        assertNotNull(e)
        assertEquals("sale", e!!.kind)
        assertNull(e.enterCode)
        assertNull(e.exitCode)
        assertTrue(e.note, e.note.contains("14:05"))
        assertEquals(2000, e.balanceYen)
    }

    @Test
    fun busWithSaleProcessKeepsNoTime() {
        // A bus record (terminal 05) with a sale-like process stays a bus with no time.
        val block = FelicaParse.histRail(
            terminal = 0x05, process = 0x46, y = 2026, m = 1, d = 2,
            inLine = 0x69, inSta = 0x40, outLine = 0, outSta = 0, bal = 500, region = 0
        )
        val e = SuicaParse.historyEntry(block)!!
        assertEquals("bus", e.kind)
        assertTrue(e.note, !e.note.contains(":"))
    }

    @Test
    fun emptyHistory() {
        assertNull(SuicaParse.historyEntry(ByteArray(16)))
    }

    @Test
    fun readCommandShape() {
        val idm = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8)
        val cmd = SuicaReader.readWithoutEncryption(idm, 0x090f, intArrayOf(0))
        assertEquals(cmd.size, cmd[0].toInt() and 0xff)
        assertEquals(0x06, cmd[1].toInt() and 0xff)
        assertEquals(0x0f, cmd[11].toInt() and 0xff)
        assertEquals(0x09, cmd[12].toInt() and 0xff)
    }
}

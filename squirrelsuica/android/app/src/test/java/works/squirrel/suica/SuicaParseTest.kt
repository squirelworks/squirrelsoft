package works.squirrel.suica

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SuicaParseTest {
    @Test
    fun balanceLittleEndian() {
        val block = byteArrayOf(0xB0.toByte(), 0x09) + ByteArray(14)
        assertEquals(2480, SuicaParse.balanceYen(block))
    }

    @Test
    fun demoCard() {
        val card = SuicaParse.demoCard()
        assertEquals(2480, card.balanceYen)
        assertEquals(16, card.idm.length)
        assertTrue(card.history.isNotEmpty())
        assertNotNull(card.history.first().date)
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
        // service little-endian 0x0f 0x09
        assertEquals(0x0f, cmd[11].toInt() and 0xff)
        assertEquals(0x09, cmd[12].toInt() and 0xff)
    }

    @Test
    fun extractBlocksFromResponse() {
        val idm = ByteArray(8) { it.toByte() }
        val block = ByteArray(16) { (it + 1).toByte() }
        block[0] = 0xB0.toByte()
        block[1] = 0x09
        val resp = ArrayList<Byte>()
        resp.add(0x07)
        idm.forEach { resp.add(it) }
        resp.add(0); resp.add(0)
        block.forEach { resp.add(it) }
        val packet = byteArrayOf((resp.size + 1).toByte()) + resp.toByteArray()
        val got = SuicaReader.extractBlocks(packet)
        assertEquals(1, got.size)
        assertEquals(2480, SuicaParse.balanceYen(got[0]))
    }
}

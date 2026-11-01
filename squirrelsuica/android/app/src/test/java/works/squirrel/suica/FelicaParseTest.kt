package works.squirrel.suica

import org.junit.Assert.assertEquals
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
}

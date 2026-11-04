package works.squirrel.suica

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException

/**
 * Reader flow against fake multi-system cards. Like a real card behind Android's
 * NfcF, a fake card ignores Polling for a system it does not have and the stack
 * reports that as a lost tag: every later command then fails too.
 */
class FelicaReaderTest {
    private class FakeSystemCard(
        /** System code -> (service code -> blocks), system 0 first. */
        private val systems: LinkedHashMap<Int, Map<Int, List<ByteArray>>>,
        private val answersSystemCodeRequest: Boolean = true
    ) : FelicaReader.Transceiver {
        val sent = ArrayList<String>()
        var lost = false
        private var current = systems.keys.first()

        private fun idmOf(sc: Int) = ByteArray(8) { (sc + it).toByte() }
        private val pmm = ByteArray(8) { 0x33 }

        private fun frame(vararg parts: ByteArray): ByteArray {
            val body = parts.fold(ByteArray(0)) { a, b -> a + b }
            return byteArrayOf((body.size + 1).toByte()) + body
        }

        override fun transceive(command: ByteArray): ByteArray {
            if (lost) throw IOException("Tag was lost.")
            val c = command.map { it.toInt() and 0xff }
            when (c[1]) {
                0x00 -> {
                    val sc = (c[2] shl 8) or c[3]
                    sent.add("poll %04x".format(sc))
                    val hit = if (sc == 0xffff) systems.keys.first() else sc.takeIf { it in systems }
                    if (hit == null) {
                        lost = true
                        throw IOException("Tag was lost.")
                    }
                    current = hit
                    val rd = if (c[4] == 1) byteArrayOf((hit shr 8).toByte(), hit.toByte()) else ByteArray(0)
                    return frame(byteArrayOf(0x01), idmOf(hit), pmm, rd)
                }
                0x0c -> {
                    sent.add("request system code")
                    if (!answersSystemCodeRequest) return ByteArray(0)
                    val list = systems.keys.flatMap { listOf((it shr 8).toByte(), it.toByte()) }
                    return frame(byteArrayOf(0x0d), command.copyOfRange(2, 10), byteArrayOf(systems.size.toByte()), list.toByteArray())
                }
                0x06 -> {
                    val svc = c[11] or (c[12] shl 8)
                    sent.add("read %04x".format(svc))
                    val n = c[13]
                    val wanted = (0 until n).map { c[15 + 2 * it] }
                    val data = systems.getValue(current)[svc]
                    val idm = command.copyOfRange(2, 10)
                    if (data == null || wanted.any { it >= data.size }) {
                        return frame(byteArrayOf(0x07), idm, byteArrayOf(0x01, 0xA6.toByte()))
                    }
                    val blocks = wanted.map { data[it] }.fold(ByteArray(0)) { a, b -> a + b }
                    return frame(byteArrayOf(0x07), idm, byteArrayOf(0, 0, n.toByte()), blocks)
                }
                else -> throw IOException("unexpected command %02x".format(c[1]))
            }
        }
    }

    private val nanacoServices = mapOf(
        0x5597 to listOf(byteArrayOf(0x50, 0x14, 0x00, 0x00) + ByteArray(12)),
        0x564f to listOf(
            FelicaParse.histNanaco(type = 0x47, amount = 480, bal = 5200, y = 2026, m = 10, d = 28, hh = 18, mm = 42),
            FelicaParse.histNanaco(type = 0x6f, amount = 3000, bal = 5680, y = 2026, m = 10, d = 27, hh = 12, mm = 5)
        )
    )

    @Test
    fun nanacoOnlyCardIsReadWithoutPollingSuica() {
        // Squirrel's nanaco dump: Polling 0xFFFF names system 0x04C7, and the old
        // Polling 0x0003 got no answer, which Android reported as "Tag was lost."
        val card = FakeSystemCard(linkedMapOf(0x04c7 to nanacoServices))
        val result = FelicaReader.readCard(card, ByteArray(8) { 0x04 })
        assertEquals(FelicaBrand.NANACO, result.brand)
        assertEquals(5200, result.balanceYen)
        assertEquals(2, result.history.size)
        assertEquals("04c7", result.systemCode)
        assertFalse(card.lost)
        assertTrue(card.sent.none { it == "poll 0003" || it == "poll fe00" })
    }

    @Test
    fun nanacoInCommonAreaStillReadFromFe00() {
        val card = FakeSystemCard(linkedMapOf(0xfe00 to nanacoServices))
        val result = FelicaReader.readCard(card, ByteArray(8) { 0x0f })
        assertEquals(FelicaBrand.NANACO, result.brand)
        assertEquals("fe00", result.systemCode)
        assertFalse(card.lost)
    }

    @Test
    fun withoutSystemCodeReplyWildcardSystemZeroDecides() {
        val card = FakeSystemCard(linkedMapOf(0x04c7 to nanacoServices), answersSystemCodeRequest = false)
        val result = FelicaReader.readCard(card, ByteArray(8) { 0x04 })
        assertEquals(FelicaBrand.NANACO, result.brand)
        assertFalse(card.lost)
        assertTrue("poll ffff" in card.sent)
    }

    @Test
    fun suicaCardStillReadsTransitFirst() {
        val hist = listOf(
            FelicaParse.histRail(
                terminal = 0x16, process = 0x01, y = 2026, m = 10, d = 28,
                inLine = 0xe3, inSta = 0x59, outLine = 0xe3, outSta = 0x5e, bal = 2480, region = 0
            )
        )
        val card = FakeSystemCard(
            linkedMapOf(0x0003 to mapOf(0x090f to hist), 0xfe00 to emptyMap(), 0x86a7 to emptyMap())
        )
        val result = FelicaReader.readCard(card, ByteArray(8) { 0x03 })
        assertEquals(FelicaBrand.SUICA_FAMILY, result.brand)
        assertEquals(2480, result.balanceYen)
        assertEquals("poll 0003", card.sent[1])
    }

    @Test
    fun systemCodeParsers() {
        val idm = ByteArray(8) { 1 }
        assertEquals(listOf(10, 0x0c) + List(8) { 1 }, FelicaReader.requestSystemCodeCommand(idm).map { it.toInt() and 0xff })
        val resp = byteArrayOf(15, 0x0d) + idm + byteArrayOf(2, 0x00, 0x03, 0xfe.toByte(), 0x00)
        assertEquals(listOf(0x0003, 0xfe00), FelicaReader.parseSystemCodes(resp))
        assertNull(FelicaReader.parseSystemCodes(byteArrayOf(11, 0x0d) + idm + byteArrayOf(0)))
        val poll = byteArrayOf(20, 0x01) + idm + ByteArray(8) + byteArrayOf(0x04, 0xc7.toByte())
        assertEquals(0x04c7, FelicaReader.parsePollingSystemCode(poll))
        assertNull(FelicaReader.parsePollingSystemCode(byteArrayOf(18, 0x01) + idm + ByteArray(8)))
    }
}

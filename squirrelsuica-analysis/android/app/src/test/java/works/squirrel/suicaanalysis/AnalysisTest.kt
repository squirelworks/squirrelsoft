package works.squirrel.suicaanalysis

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException

private fun ints(b: ByteArray) = b.map { it.toInt() and 0xff }

class FelicaProtocolTest {
    private val idm = ByteArray(8) { (0x10 + it).toByte() }

    @Test
    fun pollingFrame() {
        assertEquals(listOf(6, 0x00, 0x00, 0x03, 0x01, 0x00), ints(FelicaProtocol.polling(0x0003, 1)))
        assertEquals(listOf(6, 0x00, 0xff, 0xff, 0x02, 0x00), ints(FelicaProtocol.polling(0xffff, 2)))
    }

    @Test
    fun requestSystemCodeFrame() {
        val c = ints(FelicaProtocol.requestSystemCode(idm))
        assertEquals(10, c[0])
        assertEquals(0x0c, c[1])
        assertEquals(ints(idm), c.subList(2, 10))
    }

    @Test
    fun searchServiceFrameIsLittleEndianIndex() {
        val c = ints(FelicaProtocol.searchServiceCode(idm, 0x0102))
        assertEquals(12, c[0])
        assertEquals(0x0a, c[1])
        assertEquals(listOf(0x02, 0x01), c.subList(10, 12))
    }

    @Test
    fun readFrameTwoAndThreeByteElements() {
        val c = ints(FelicaProtocol.readWithoutEncryption(idm, 0x090f, listOf(0, 1, 255, 300)))
        assertEquals(0x06, c[1])
        assertEquals(1, c[10]) // one service
        assertEquals(listOf(0x0f, 0x09), c.subList(11, 13))
        assertEquals(4, c[13])
        assertEquals(listOf(0x80, 0x00, 0x80, 0x01, 0x80, 0xff, 0x00, 0x2c, 0x01), c.subList(14, 23))
        assertEquals(c.size, c[0])
    }

    @Test
    fun requestServiceFrame() {
        val c = ints(FelicaProtocol.requestService(idm, listOf(0x090f, 0x008b)))
        assertEquals(0x02, c[1])
        assertEquals(listOf(2, 0x0f, 0x09, 0x8b, 0x00), c.subList(10, 15))
    }

    @Test
    fun readResponseStatusFlags() {
        val bad = byteArrayOf(12, 0x07) + idm + byteArrayOf(0x01, 0xa8.toByte())
        val r = FelicaProtocol.parseRead(bad)!!
        assertFalse(r.ok)
        assertEquals(0x01, r.sf1)
        assertEquals(0xa8, r.sf2)
        val good = byteArrayOf(0, 0x07) + idm + byteArrayOf(0, 0, 2) + ByteArray(32) { it.toByte() }
        good[0] = good.size.toByte()
        val g = FelicaProtocol.parseRead(good)!!
        assertTrue(g.ok)
        assertEquals(2, g.blocks.size)
        assertEquals(16, g.blocks[1][0].toInt())
    }

    @Test
    fun searchServiceAreaServiceAndEnd() {
        fun resp(vararg d: Int) = (byteArrayOf(0, 0x0b) + idm + d.map { it.toByte() }.toByteArray())
            .also { it[0] = it.size.toByte() }
        assertEquals(FelicaProtocol.SearchResult.Service(0x090f), FelicaProtocol.parseSearchService(resp(0x0f, 0x09)))
        assertEquals(
            FelicaProtocol.SearchResult.Area(0x0000, 0xfffe),
            FelicaProtocol.parseSearchService(resp(0x00, 0x00, 0xfe, 0xff))
        )
        assertEquals(FelicaProtocol.SearchResult.End, FelicaProtocol.parseSearchService(resp(0xff, 0xff)))
    }

    @Test
    fun serviceAttributes() {
        assertTrue(FelicaProtocol.isReadableWithoutKey(0x090f)) // cyclic RO, no key
        assertEquals("cyclic-ro", FelicaProtocol.serviceKind(0x090f))
        assertTrue(FelicaProtocol.isReadableWithoutKey(0x008b)) // random RO, no key
        assertFalse(FelicaProtocol.isReadableWithoutKey(0x1008)) // random RW, key
        assertTrue(FelicaProtocol.isArea(0x0000))
        assertTrue(FelicaProtocol.isArea(0x0041))
        assertFalse(FelicaProtocol.isReadableWithoutKey(0x0001))
    }

    @Test
    fun pmmDecode() {
        // Parameter 0x4b = E=1, B=1, A=3: T × (2 + 4) × 4 ≈ 7.25 ms.
        val pmm = byteArrayOf(0x01, 0x20, 0x22, 0x04, 0x27, 0x4b, 0x42, 0x83.toByte())
        val info = FelicaProtocol.decodePmm(pmm)!!
        assertEquals(0x01, info.romType)
        assertEquals(0x20, info.icType)
        assertEquals(FelicaProtocol.PMM_COMMAND_CLASSES, info.timeoutsMs.keys.toList())
        assertEquals(FelicaProtocol.T_MS * 6 * 4, info.timeoutsMs["read"]!!, 1e-9)
        assertEquals(0.302, FelicaProtocol.T_MS, 0.001)
        assertNull(FelicaProtocol.decodePmm(ByteArray(7)))
    }
}

/**
 * Fake card: system 0x0003 with an area, history 0x090f (20 blocks, max 4 per
 * read), info 0x008b (1 block) and a keyed service 0x1008; IOException after
 * [loseAfter] commands to simulate a lost tag. [systemCodes] are the systems the
 * card advertises (system 0 first); like a real card behind Android NfcF, Polling
 * for any other code gets no answer, which surfaces as a lost tag.
 */
class FakeCard(
    private val loseAfter: Int = Int.MAX_VALUE,
    private val systemCodes: List<Int> = listOf(0x0003),
    private val nodes: List<IntArray> =
        listOf(intArrayOf(0x0000, 0xfffe), intArrayOf(0x090f), intArrayOf(0x008b), intArrayOf(0x1008)),
    private val sizes: Map<Int, Int> = mapOf(0x090f to 20, 0x008b to 1)
) : Transceiver {
    val idm = byteArrayOf(0x01, 0x01, 0x12, 0x34, 0x56, 0x78, 0x9a.toByte(), 0xbc.toByte())
    val pmm = byteArrayOf(0x10, 0x0b, 0x4b, 0x42, 0x84.toByte(), 0x85.toByte(), 0x10, 0xff.toByte())
    var count = 0

    private fun reply(code: Int, data: List<Int>): ByteArray {
        val b = (byteArrayOf(0, code.toByte()) + idm + data.map { it.toByte() }.toByteArray())
        b[0] = b.size.toByte()
        return b
    }

    override fun transceive(command: ByteArray): ByteArray {
        if (++count > loseAfter) throw IOException("Tag was lost.")
        val c = ints(command)
        require(c[0] == c.size)
        return when (c[1]) {
            0x00 -> {
                val sc = (c[2] shl 8) or c[3]
                if (sc != 0xffff && sc !in systemCodes) throw IOException("Tag was lost.")
                val sys = if (sc == 0xffff) systemCodes[0] else sc
                val rd = when (c[4]) { 1 -> listOf(sys shr 8, sys and 0xff); 2 -> listOf(0x00, 0x83); else -> emptyList() }
                val b = (byteArrayOf(0, 0x01) + idm + pmm + rd.map { it.toByte() }.toByteArray())
                b[0] = b.size.toByte()
                b
            }
            0x0c -> reply(0x0d, listOf(systemCodes.size) + systemCodes.flatMap { listOf(it shr 8, it and 0xff) })
            0x04 -> reply(0x05, listOf(0))
            0x0a -> {
                val idx = c[10] or (c[11] shl 8)
                val node = nodes.getOrNull(idx) ?: return reply(0x0b, listOf(0xff, 0xff))
                reply(0x0b, node.flatMap { listOf(it and 0xff, it shr 8) })
            }
            0x02 -> {
                val n = c[10]
                reply(0x03, listOf(n) + (0 until n).flatMap { listOf(0x00, 0x00) })
            }
            0x06 -> {
                val svc = c[11] or (c[12] shl 8)
                val n = c[13]
                val blocks = ArrayList<Int>()
                var off = 14
                repeat(n) {
                    if (c[off] and 0x80 != 0) { blocks.add(c[off + 1]); off += 2 } else { blocks.add(c[off + 1] or (c[off + 2] shl 8)); off += 3 }
                }
                val size = sizes[svc] ?: return reply(0x07, listOf(0x01, 0xa6))
                if (n > 4) return reply(0x07, listOf(0x01, 0xa2))
                if (blocks.any { it >= size }) return reply(0x07, listOf(0x01, 0xa8))
                reply(0x07, listOf(0, 0, n) + blocks.flatMap { b -> listOf(b) + List(15) { svc and 0xff } })
            }
            else -> byteArrayOf()
        }
    }
}

class CardAnalyzerTest {
    private val fixedClock = { 0L }

    @Test
    fun walksSystemsServicesAndChunkSizes() {
        val card = FakeCard()
        val dump = CardAnalyzer(card, fixedClock, maxBlocks = 32).analyze(null, "2026-11-04T00:00:00Z", "test")
        assertNull(dump.error)
        assertEquals(card.idm.toHex(), dump.idm)
        assertEquals(listOf(0x0003), dump.systemCodes)
        // Wildcard plus the one advertised system, rc=1 and rc=2 each; no blind 0xFE00.
        assertEquals(listOf(0xffff, 0xffff, 0x0003, 0x0003), dump.polls.map { it.systemCode })
        assertEquals(0x0003, dump.polls.first { it.systemCode == 0xffff && it.requestCode == 1 }.requestData)
        val sys = dump.systems.single()
        assertEquals(listOf(AreaDump(0x0000, 0xfffe)), sys.areas)
        assertEquals(listOf(0x090f, 0x008b, 0x1008), sys.services.map { it.code })

        val hist = sys.services[0]
        assertTrue(hist.readable)
        assertEquals(20, hist.blocks.size)
        assertEquals(0, hist.keyVersion)
        val byChunk = hist.attempts.associateBy { it.chunkSize }
        assertEquals(listOf(1, 2, 4, 8, 12, 15), hist.attempts.map { it.chunkSize })
        assertEquals(20, byChunk[1]!!.blocksRead)
        assertEquals(20, byChunk[1]!!.stoppedAt)
        assertEquals(0xa8, byChunk[1]!!.sf2)
        assertEquals(20, byChunk[4]!!.blocksRead)
        // The card caps reads at 4 blocks: chunk 8 is refused at block 0 with SF2=A2.
        assertEquals(0, byChunk[8]!!.blocksRead)
        assertEquals(0, byChunk[8]!!.stoppedAt)
        assertEquals(0xa2, byChunk[8]!!.sf2)

        assertEquals(1, sys.services[1].blocks.size)
        val keyed = sys.services[2]
        assertFalse(keyed.readable)
        assertTrue(keyed.attempts.isEmpty())
    }

    @Test
    fun nanacoOnlyCardIsNeverPolledForSuicaOrCommonArea() {
        // Squirrel's nanaco dump: Polling 0xFFFF answers with system 0x04C7, and
        // Polling 0x0003 was met with silence that Android reported as "Tag was lost."
        val card = FakeCard(systemCodes = listOf(0x04c7))
        val dump = CardAnalyzer(card, fixedClock, maxBlocks = 8).analyze(null, "t", "test")
        assertNull(dump.error)
        assertEquals(listOf(0x04c7), dump.systemCodes)
        assertEquals(listOf(0xffff, 0xffff, 0x04c7, 0x04c7), dump.polls.map { it.systemCode })
        assertTrue(dump.log.none { it.name.startsWith("polling 0003") || it.name.startsWith("polling fe00") })
        assertEquals(listOf(0x04c7), dump.systems.map { it.systemCode })
    }

    @Test
    fun nanacoPurseAndHistoryAreDumpedUnder04c7() {
        // nanaco balance 0x5597 (purse, read-only) and history 0x564F (cyclic, read-only)
        // need no key, so the dump must carry their blocks.
        val card = FakeCard(
            systemCodes = listOf(0x04c7),
            nodes = listOf(intArrayOf(0x0000, 0xfffe), intArrayOf(0x5597), intArrayOf(0x564f)),
            sizes = mapOf(0x5597 to 1, 0x564f to 5)
        )
        val sys = CardAnalyzer(card, fixedClock, maxBlocks = 8).analyze(null, "t", "test").systems.single()
        assertEquals(0x04c7, sys.systemCode)
        val bal = sys.services.single { it.code == 0x5597 }
        assertEquals("purse-ro", bal.kind)
        assertTrue(bal.readable)
        assertEquals(1, bal.blocks.size)
        val hist = sys.services.single { it.code == 0x564f }
        assertEquals("cyclic-ro", hist.kind)
        assertEquals(5, hist.blocks.size)
    }

    @Test
    fun noSystemCodeReplyFallsBackToWildcardSystemZero() {
        // If Request System Code fails, only the code named by Polling 0xFFFF rc=1 is polled.
        val card = object : Transceiver {
            val inner = FakeCard(systemCodes = listOf(0x04c7))
            override fun transceive(command: ByteArray): ByteArray =
                if (command[1].toInt() == 0x0c) byteArrayOf() else inner.transceive(command)
        }
        val dump = CardAnalyzer(card, fixedClock, maxBlocks = 8).analyze(null, "t", "test")
        assertNull(dump.error)
        assertTrue(dump.systemCodes.isEmpty())
        assertEquals(listOf(0xffff, 0xffff, 0x04c7, 0x04c7), dump.polls.map { it.systemCode })
    }

    @Test
    fun lostTagKeepsPartialDump() {
        val dump = CardAnalyzer(FakeCard(loseAfter = 12), fixedClock).analyze(null, "t", "test")
        assertNotNull(dump.error)
        assertTrue(dump.error!!.contains("lost"))
        assertEquals(12 + 1, dump.log.size)
        assertNotNull(dump.log.last().error)
    }
}

class DumpJsonTest {
    private fun dump() = CardAnalyzer(FakeCard(), { 0L }, maxBlocks = 24).analyze(null, "2026-11-04T00:00:00Z", "test")

    @Test
    fun jsonFollowsSchemaShape() {
        val tree = MiniJson.parse(DumpJson.toJson(dump())) as Map<*, *>
        assertEquals(DumpJson.SCHEMA, tree["schema"])
        for (k in listOf("tool", "created_at", "idm", "pmm", "pmm_decoded", "polls", "system_codes", "systems", "log", "error")) {
            assertTrue("missing $k", tree.containsKey(k))
        }
        val systems = tree["systems"] as List<*>
        val sys = systems[0] as Map<*, *>
        assertEquals("0x0003", sys["system_code"])
        val svc = (sys["services"] as List<*>)[0] as Map<*, *>
        assertEquals("0x090f", svc["code"])
        assertEquals(20, (svc["blocks"] as List<*>).size)
        assertEquals(32, ((svc["blocks"] as List<*>)[0] as String).length)
        val attempt = (svc["attempts"] as List<*>)[0] as Map<*, *>
        for (k in listOf("chunk_size", "blocks_read", "stopped_at", "sf1", "sf2", "error", "commands", "avg_ms")) {
            assertTrue("attempt missing $k", attempt.containsKey(k))
        }
        val pd = tree["pmm_decoded"] as Map<*, *>
        assertEquals(FelicaProtocol.PMM_COMMAND_CLASSES, (pd["max_response_ms"] as Map<*, *>).keys.toList())
    }

    @Test
    fun redactionRemovesIdmEverywhere() {
        val d = dump()
        val json = DumpJson.toJson(d, redactIdm = true)
        assertFalse(json.contains(d.idm!!))
        val tree = MiniJson.parse(json) as Map<*, *>
        assertEquals(DumpJson.REDACTED, tree["idm"])
        assertTrue(DumpJson.toJson(d).contains(d.idm!!))
    }
}

/** Tiny JSON reader for tests only. */
object MiniJson {
    fun parse(s: String): Any? {
        val p = P(s)
        val v = p.value()
        p.ws()
        require(p.i == s.length) { "trailing data at ${p.i}" }
        return v
    }

    private class P(val s: String) {
        var i = 0
        fun ws() { while (i < s.length && s[i].isWhitespace()) i++ }
        fun value(): Any? {
            ws()
            return when (s[i]) {
                '{' -> obj()
                '[' -> arr()
                '"' -> str()
                't' -> { expect("true"); true }
                'f' -> { expect("false"); false }
                'n' -> { expect("null"); null }
                else -> num()
            }
        }
        fun expect(w: String) { require(s.startsWith(w, i)) { "expected $w at $i" }; i += w.length }
        fun obj(): Map<String, Any?> {
            val m = LinkedHashMap<String, Any?>()
            i++; ws()
            if (s[i] == '}') { i++; return m }
            while (true) {
                ws(); val k = str(); ws(); expect(":")
                m[k] = value(); ws()
                if (s[i] == ',') { i++; continue }
                expect("}"); return m
            }
        }
        fun arr(): List<Any?> {
            val l = ArrayList<Any?>()
            i++; ws()
            if (s[i] == ']') { i++; return l }
            while (true) {
                l.add(value()); ws()
                if (s[i] == ',') { i++; continue }
                expect("]"); return l
            }
        }
        fun str(): String {
            expect("\"")
            val sb = StringBuilder()
            while (s[i] != '"') {
                if (s[i] == '\\') {
                    i++
                    when (s[i]) {
                        'n' -> sb.append('\n'); 't' -> sb.append('\t'); 'r' -> sb.append('\r')
                        'u' -> { sb.append(s.substring(i + 1, i + 5).toInt(16).toChar()); i += 4 }
                        else -> sb.append(s[i])
                    }
                    i++
                } else sb.append(s[i++])
            }
            i++
            return sb.toString()
        }
        fun num(): Any {
            val st = i
            while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++
            val t = s.substring(st, i)
            return t.toIntOrNull() ?: t.toDouble()
        }
    }
}

class DumpTextTest {
    @Test
    fun summaryAndLogRender() {
        val d = CardAnalyzer(FakeCard(), { 0L }, maxBlocks = 24).analyze(null, "t", "test")
        val s = DumpText.summary(d)
        assertTrue(s.contains("System 0003"))
        assertTrue(s.contains("svc 090f cyclic-ro"))
        assertTrue(s.contains("SF 01/a2"))
        assertTrue(s.contains("svc 1008 random-rw       key required"))
        val l = DumpText.log(d)
        assertEquals(d.log.size * 3, l.trimEnd().lines().size)
    }
}

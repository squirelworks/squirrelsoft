package works.squirrel.suica

import android.nfc.Tag
import android.nfc.tech.NfcF
import java.io.IOException
import kotlin.math.min

/**
 * Read-only multi-brand FeliCa access via Android [NfcF].
 *
 * Tries transit (0x0003) then common-area e-money (0xFE00 / 0x04C7).
 * Balance is read first with few round-trips to reduce "Tag was lost".
 *
 * Suica history lives on service **0x090f** (not 0x010f). Each 16-byte
 * history block carries the post-transaction balance at bytes 10–11 LE.
 */
object FelicaReader {
    private const val SYS_TRANSIT = 0x0003
    private const val SYS_COMMON = 0xFE00
    private const val SYS_NANACO = 0x04C7

    /** Suica/PASMO journey history (also the live balance source). */
    private const val SVC_SUICA_HIST = 0x090f
    private const val SVC_SUICA_INFO = 0x008b
    private const val SVC_NANACO_BAL = 0x5597
    private const val SVC_NANACO_HIST = 0x564f
    private const val SVC_EDY_BAL = 0x1317
    private const val SVC_EDY_HIST = 0x170f
    private const val SVC_WAON_BAL = 0x6817
    private const val SVC_WAON_HIST = 0x680b

    /** FeliCa Read Without Encryption is reliable in chunks of ≤4 blocks. */
    private const val CHUNK = 4

    fun read(tag: Tag): FelicaCard {
        val nfc = NfcF.get(tag) ?: throw IOException("Not FeliCa / NfcF")
        nfc.connect()
        try {
            val idmTag = nfc.tag.id ?: throw IOException("missing IDm")
            val idmHex = idmTag.joinToString("") { "%02x".format(it) }

            // 1) Transit IC (Suica/PASMO/…)
            tryPoll(nfc, SYS_TRANSIT)?.let { (idm, pmm) ->
                // History service 0x090f first (up to 20 blocks); balance is in each record.
                val hist = readServiceRange(nfc, idm, SVC_SUICA_HIST, 0, 20)
                val b008b = readBlocks(nfc, idm, SVC_SUICA_INFO, intArrayOf(0)).getOrNull(0)
                val bal = FelicaParse.suicaBalance(hist.firstOrNull(), b008b)
                if (bal != null || hist.isNotEmpty()) {
                    return FelicaCard(
                        brand = FelicaBrand.SUICA_FAMILY,
                        brandLabel = FelicaParse.brandLabel(FelicaBrand.SUICA_FAMILY),
                        idm = idm.joinToString("") { "%02x".format(it) },
                        pmm = pmm.joinToString("") { "%02x".format(it) },
                        systemCode = "%04x".format(SYS_TRANSIT),
                        balanceYen = bal,
                        history = FelicaParse.historyEntries(hist),
                        note = "Live transit IC · hold still while reading"
                    )
                }
            }

            // 2) Common area (Edy / WAON / nanaco)
            tryPoll(nfc, SYS_COMMON)?.let { (idm, pmm) ->
                purseFromCommon(nfc, idm, pmm, SYS_COMMON)?.let { return it }
            }

            // 3) Dedicated nanaco system
            tryPoll(nfc, SYS_NANACO)?.let { (idm, pmm) ->
                val blocks = readBlocks(nfc, idm, SVC_NANACO_BAL, intArrayOf(0))
                if (blocks.isNotEmpty()) {
                    val hist = readServiceRange(nfc, idm, SVC_NANACO_HIST, 0, 5)
                    return FelicaCard(
                        brand = FelicaBrand.NANACO,
                        brandLabel = FelicaParse.brandLabel(FelicaBrand.NANACO),
                        idm = idm.joinToString("") { "%02x".format(it) },
                        pmm = pmm.joinToString("") { "%02x".format(it) },
                        systemCode = "%04x".format(SYS_NANACO),
                        balanceYen = FelicaParse.nanacoBalance(blocks[0]),
                        history = FelicaParse.nanacoHistoryEntries(hist),
                        note = "Live nanaco"
                    )
                }
            }

            return FelicaCard(
                brand = FelicaBrand.UNKNOWN,
                brandLabel = FelicaParse.brandLabel(FelicaBrand.UNKNOWN),
                idm = idmHex,
                pmm = "",
                systemCode = "????",
                balanceYen = null,
                history = emptyList(),
                note = "FeliCa seen, but no supported purse (Suica/nanaco/Edy/WAON). " +
                    "Fixture tests cover layouts — physical samples welcome."
            )
        } finally {
            try {
                nfc.close()
            } catch (_: Exception) {
            }
        }
    }

    private fun purseFromCommon(
        nfc: NfcF,
        idm: ByteArray,
        pmm: ByteArray,
        systemCode: Int
    ): FelicaCard? {
        val idmHex = idm.joinToString("") { "%02x".format(it) }
        val pmmHex = pmm.joinToString("") { "%02x".format(it) }
        val sys = "%04x".format(systemCode)

        readBlocks(nfc, idm, SVC_NANACO_BAL, intArrayOf(0)).getOrNull(0)?.let { block ->
            val hist = readServiceRange(nfc, idm, SVC_NANACO_HIST, 0, 5)
            return FelicaCard(
                brand = FelicaBrand.NANACO,
                brandLabel = FelicaParse.brandLabel(FelicaBrand.NANACO),
                idm = idmHex,
                pmm = pmmHex,
                systemCode = sys,
                balanceYen = FelicaParse.nanacoBalance(block),
                history = FelicaParse.nanacoHistoryEntries(hist),
                note = "Live nanaco (common area)"
            )
        }
        readBlocks(nfc, idm, SVC_EDY_BAL, intArrayOf(0)).getOrNull(0)?.let { block ->
            val hist = readServiceRange(nfc, idm, SVC_EDY_HIST, 0, 6)
            return FelicaCard(
                brand = FelicaBrand.EDY,
                brandLabel = FelicaParse.brandLabel(FelicaBrand.EDY),
                idm = idmHex,
                pmm = pmmHex,
                systemCode = sys,
                balanceYen = FelicaParse.edyBalance(block),
                history = FelicaParse.edyHistoryEntries(hist),
                note = "Live 楽天Edy"
            )
        }
        readBlocks(nfc, idm, SVC_WAON_BAL, intArrayOf(0)).getOrNull(0)?.let { block ->
            // WAON history: three 32-byte records across blocks 0–5.
            val histBlocks = readServiceRange(nfc, idm, SVC_WAON_HIST, 0, 6)
            return FelicaCard(
                brand = FelicaBrand.WAON,
                brandLabel = FelicaParse.brandLabel(FelicaBrand.WAON),
                idm = idmHex,
                pmm = pmmHex,
                systemCode = sys,
                balanceYen = FelicaParse.waonBalance(block),
                history = FelicaParse.waonHistoryEntries(histBlocks),
                note = "Live WAON"
            )
        }
        return null
    }

    private fun tryPoll(nfc: NfcF, systemCode: Int): Pair<ByteArray, ByteArray>? {
        return try {
            val poll = nfc.transceive(pollingCommand(systemCode))
            if (poll.size < 10) return null
            var off = 0
            if ((poll[0].toInt() and 0xff) == poll.size || (poll[0].toInt() and 0xff) + 1 == poll.size) {
                off = 1
            }
            if (off >= poll.size || (poll[off].toInt() and 0xff) != 0x01) return null
            off += 1
            if (off + 8 > poll.size) return null
            val idm = poll.copyOfRange(off, off + 8)
            off += 8
            val pmm = if (off + 8 <= poll.size) poll.copyOfRange(off, off + 8) else ByteArray(0)
            idm to pmm
        } catch (_: Exception) {
            null
        }
    }

    /** Read [count] blocks starting at [start], in chunks of [CHUNK]. Stops on empty/short reply. */
    private fun readServiceRange(
        nfc: NfcF,
        idm: ByteArray,
        service: Int,
        start: Int,
        count: Int
    ): List<ByteArray> {
        val out = ArrayList<ByteArray>(count)
        var i = start
        val end = start + count
        while (i < end) {
            val n = min(CHUNK, end - i)
            val indices = IntArray(n) { i + it }
            val got = readBlocks(nfc, idm, service, indices)
            if (got.isEmpty()) break
            out.addAll(got)
            if (got.size < n) break
            i += n
        }
        return out
    }

    private fun readBlocks(nfc: NfcF, idm: ByteArray, service: Int, blocks: IntArray): List<ByteArray> {
        return try {
            val resp = nfc.transceive(readWithoutEncryption(idm, service, blocks))
            extractBlocks(resp)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun pollingCommand(systemCode: Int): ByteArray {
        val scHi = ((systemCode shr 8) and 0xff).toByte()
        val scLo = (systemCode and 0xff).toByte()
        return byteArrayOf(0x06, 0x00, scHi, scLo, 0x01, 0x0f)
    }

    fun readWithoutEncryption(idm: ByteArray, serviceCode: Int, blocks: IntArray): ByteArray {
        require(idm.size == 8)
        require(blocks.isNotEmpty())
        val out = ArrayList<Byte>(64)
        out.add(0)
        out.add(0x06)
        idm.forEach { out.add(it) }
        out.add(0x01)
        out.add((serviceCode and 0xff).toByte())
        out.add(((serviceCode shr 8) and 0xff).toByte())
        out.add(blocks.size.toByte())
        for (b in blocks) {
            out.add((0x80 or (b and 0xff)).toByte())
            out.add(0x00)
        }
        out[0] = out.size.toByte()
        return out.toByteArray()
    }

    fun extractBlocks(resp: ByteArray): List<ByteArray> {
        if (resp.isEmpty()) return emptyList()
        var off = 0
        if ((resp[0].toInt() and 0xff) == resp.size || (resp[0].toInt() and 0xff) + 1 == resp.size) {
            off = 1
        }
        if (off >= resp.size) return emptyList()
        val cmd = resp[off].toInt() and 0xff
        if (cmd != 0x07) return emptyList() // do not invent blocks from garbage
        off += 1
        if (off + 8 + 2 > resp.size) return emptyList()
        off += 8
        val st1 = resp[off].toInt() and 0xff
        val st2 = resp[off + 1].toInt() and 0xff
        off += 2
        if (st1 != 0 || st2 != 0) return emptyList()
        // optional block count byte on some stacks
        if (off < resp.size && (resp.size - off) % 16 == 1) off += 1
        val blocks = mutableListOf<ByteArray>()
        while (off + 16 <= resp.size) {
            blocks.add(resp.copyOfRange(off, off + 16))
            off += 16
        }
        return blocks
    }
}

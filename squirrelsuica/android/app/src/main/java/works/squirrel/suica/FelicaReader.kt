package works.squirrel.suica

import android.nfc.Tag
import android.nfc.tech.NfcF
import java.io.IOException
import kotlin.math.min

/**
 * Read-only multi-brand FeliCa access via Android [NfcF].
 *
 * Clean-room approach aligned with public Suica readers (nfcpy / metrodroid /
 * FeliScan notes — not a verbatim copy of any app):
 * 1. Poll system 0x0003 (or use Tag IDm when the stack already selected it)
 * 2. Read history service **0x090f** (≤20 blocks)
 * 3. Parse 16-byte records; balance at bytes 10–11 LE
 *
 * Chunked multi-block reads are attempted first for speed, then we fall back to
 * single-block reads when the controller/tag drops the multi-block command —
 * the failure mode that previously left History as a blank "(empty)".
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

    /** FeliCa Read Without Encryption: try ≤4 blocks, then fall back to 1. */
    private const val CHUNK = 4
    private const val SUICA_HIST_BLOCKS = 20
    private const val NFC_TIMEOUT_MS = 5000

    /** One FeliCa command/response exchange; [NfcF.transceive] in production, a fake in tests. */
    fun interface Transceiver {
        fun transceive(command: ByteArray): ByteArray
    }

    internal data class BlockRead(
        val blocks: List<ByteArray>,
        val status: HistoryStatus,
        val message: String = ""
    )

    fun read(tag: Tag): FelicaCard {
        val nfc = NfcF.get(tag) ?: throw IOException("Not FeliCa / NfcF")
        nfc.connect()
        try {
            try {
                nfc.timeout = NFC_TIMEOUT_MS
            } catch (_: Exception) {
            }

            val idmTag = nfc.tag.id ?: throw IOException("missing IDm")
            return readCard(Transceiver { nfc.transceive(it) }, idmTag)
        } finally {
            try {
                nfc.close()
            } catch (_: Exception) {
            }
        }
    }

    /**
     * Identify and read the card behind [tx]. [tagIdm] is the IDm Android discovered.
     *
     * Only system codes the card advertises are polled. A FeliCa card stays silent
     * when polled for a system it does not have, and Android's NfcF turns that
     * silence into a lost tag, after which every later command fails. A nanaco card
     * (system 0x04C7 only) used to die on the first Polling(0x0003) this way.
     */
    internal fun readCard(tx: Transceiver, tagIdm: ByteArray): FelicaCard {
        val idmHex = tagIdm.joinToString("") { "%02x".format(it) }
        val systems = advertisedSystems(tx, tagIdm)
        fun has(sc: Int) = systems == null || sc in systems

        // 1) Transit IC (Suica/PASMO/…)
        if (has(SYS_TRANSIT)) readTransit(tx, tagIdm)?.let { return it }

        // 2) Common area (Edy / WAON / nanaco)
        if (has(SYS_COMMON)) {
            tryPoll(tx, SYS_COMMON)?.let { (idm, pmm) ->
                purseFromCommon(tx, idm, pmm, SYS_COMMON)?.let { return it }
            }
        }

        // 3) Dedicated nanaco system
        if (has(SYS_NANACO)) {
            tryPoll(tx, SYS_NANACO)?.let { (idm, pmm) ->
                val blocks = readBlocks(tx, idm, SVC_NANACO_BAL, intArrayOf(0))
                if (blocks.isNotEmpty()) {
                    val hist = readServiceRange(tx, idm, SVC_NANACO_HIST, 0, 5)
                    return FelicaCard(
                        brand = FelicaBrand.NANACO,
                        brandLabel = FelicaParse.brandLabel(FelicaBrand.NANACO),
                        idm = idm.joinToString("") { "%02x".format(it) },
                        pmm = pmm.joinToString("") { "%02x".format(it) },
                        systemCode = "%04x".format(SYS_NANACO),
                        balanceYen = FelicaParse.nanacoBalance(blocks[0]),
                        history = FelicaParse.nanacoHistoryEntries(hist.blocks),
                        note = "Live nanaco",
                        historyStatus = hist.status,
                        historyMessage = hist.message
                    )
                }
            }
        }

        return FelicaCard(
            brand = FelicaBrand.UNKNOWN,
            brandLabel = FelicaParse.brandLabel(FelicaBrand.UNKNOWN),
            idm = idmHex,
            pmm = "",
            systemCode = systems?.firstOrNull()?.let { "%04x".format(it) } ?: "????",
            balanceYen = null,
            history = emptyList(),
            note = "FeliCa seen, but no supported purse (Suica/nanaco/Edy/WAON). " +
                "Fixture tests cover layouts — physical samples welcome.",
            historyStatus = HistoryStatus.EMPTY_CARD,
            historyMessage = "No supported purse history on this tag."
        )
    }

    /**
     * The card's system codes: Request System Code on [idm], else the system 0 code
     * from a wildcard Polling(0xFFFF, rc=1), which every card answers. Null only if
     * both fail; callers then probe every system as before.
     */
    internal fun advertisedSystems(tx: Transceiver, idm: ByteArray): Set<Int>? {
        try {
            parseSystemCodes(tx.transceive(requestSystemCodeCommand(idm)))?.let { return it.toSet() }
        } catch (_: Exception) {
        }
        return try {
            parsePollingSystemCode(tx.transceive(pollingCommand(0xFFFF)))?.let { setOf(it) }
        } catch (_: Exception) {
            null
        }
    }

    /**
     * Transit read path: prefer Polling(0x0003); if that fails (some stacks already
     * selected the system), fall back to the discovery IDm from [Tag.getId].
     */
    private fun readTransit(tx: Transceiver, tagIdm: ByteArray): FelicaCard? {
        val polled = tryPoll(tx, SYS_TRANSIT)
        val idm = polled?.first ?: tagIdm
        val pmm = polled?.second ?: ByteArray(0)

        val hist = readServiceRange(tx, idm, SVC_SUICA_HIST, 0, SUICA_HIST_BLOCKS)
        val b008b = readBlocks(tx, idm, SVC_SUICA_INFO, intArrayOf(0)).getOrNull(0)
        val bal = FelicaParse.suicaBalance(hist.blocks.firstOrNull(), b008b)
        val entries = FelicaParse.historyEntries(hist.blocks)

        // Only claim transit when Polling(0x0003) worked or we actually got 0x090f/008b data.
        // A bare READ_FAILED on the discovery IDm must not steal nanaco/Edy/WAON cards.
        val sawTransit = polled != null || bal != null || hist.blocks.isNotEmpty()
        if (!sawTransit) return null

        val (status, message) = when {
            entries.isNotEmpty() && hist.status == HistoryStatus.PARTIAL ->
                HistoryStatus.PARTIAL to hist.message.ifBlank {
                    "Read ${entries.size} of up to $SUICA_HIST_BLOCKS — hold still and scan again."
                }
            entries.isNotEmpty() -> HistoryStatus.OK to ""
            hist.status == HistoryStatus.READ_FAILED ->
                HistoryStatus.READ_FAILED to hist.message.ifBlank {
                    "Could not read Suica history service 0x090f.\n" +
                        "Hold the card still on the NFC spot and try again."
                }
            hist.status == HistoryStatus.PARTIAL ->
                HistoryStatus.PARTIAL to hist.message.ifBlank {
                    "History read interrupted before any usable rows.\nHold still and scan again."
                }
            else -> HistoryStatus.EMPTY_CARD to
                "History service responded, but no journey records yet."
        }

        val note = when (status) {
            HistoryStatus.READ_FAILED, HistoryStatus.PARTIAL ->
                "Live transit IC · history incomplete — hold still and rescan"
            else -> "Live transit IC · hold still while reading"
        }

        return FelicaCard(
            brand = FelicaBrand.SUICA_FAMILY,
            brandLabel = FelicaParse.brandLabel(FelicaBrand.SUICA_FAMILY),
            idm = idm.joinToString("") { "%02x".format(it) },
            pmm = pmm.joinToString("") { "%02x".format(it) },
            systemCode = "%04x".format(SYS_TRANSIT),
            balanceYen = bal,
            history = entries,
            note = note,
            historyStatus = status,
            historyMessage = message
        )
    }

    private fun purseFromCommon(
        tx: Transceiver,
        idm: ByteArray,
        pmm: ByteArray,
        systemCode: Int
    ): FelicaCard? {
        val idmHex = idm.joinToString("") { "%02x".format(it) }
        val pmmHex = pmm.joinToString("") { "%02x".format(it) }
        val sys = "%04x".format(systemCode)

        readBlocks(tx, idm, SVC_NANACO_BAL, intArrayOf(0)).getOrNull(0)?.let { block ->
            val hist = readServiceRange(tx, idm, SVC_NANACO_HIST, 0, 5)
            val entries = FelicaParse.nanacoHistoryEntries(hist.blocks)
            return FelicaCard(
                brand = FelicaBrand.NANACO,
                brandLabel = FelicaParse.brandLabel(FelicaBrand.NANACO),
                idm = idmHex,
                pmm = pmmHex,
                systemCode = sys,
                balanceYen = FelicaParse.nanacoBalance(block),
                history = entries,
                note = "Live nanaco (common area)",
                historyStatus = resolvePurseHistoryStatus(entries, hist),
                historyMessage = hist.message
            )
        }
        readBlocks(tx, idm, SVC_EDY_BAL, intArrayOf(0)).getOrNull(0)?.let { block ->
            val hist = readServiceRange(tx, idm, SVC_EDY_HIST, 0, 6)
            val entries = FelicaParse.edyHistoryEntries(hist.blocks)
            return FelicaCard(
                brand = FelicaBrand.EDY,
                brandLabel = FelicaParse.brandLabel(FelicaBrand.EDY),
                idm = idmHex,
                pmm = pmmHex,
                systemCode = sys,
                balanceYen = FelicaParse.edyBalance(block),
                history = entries,
                note = "Live 楽天Edy",
                historyStatus = resolvePurseHistoryStatus(entries, hist),
                historyMessage = hist.message
            )
        }
        readBlocks(tx, idm, SVC_WAON_BAL, intArrayOf(0)).getOrNull(0)?.let { block ->
            val histBlocks = readServiceRange(tx, idm, SVC_WAON_HIST, 0, 6)
            val entries = FelicaParse.waonHistoryEntries(histBlocks.blocks)
            return FelicaCard(
                brand = FelicaBrand.WAON,
                brandLabel = FelicaParse.brandLabel(FelicaBrand.WAON),
                idm = idmHex,
                pmm = pmmHex,
                systemCode = sys,
                balanceYen = FelicaParse.waonBalance(block),
                history = entries,
                note = "Live WAON",
                historyStatus = resolvePurseHistoryStatus(entries, histBlocks),
                historyMessage = histBlocks.message
            )
        }
        return null
    }

    private fun resolvePurseHistoryStatus(
        entries: List<HistoryEntry>,
        hist: BlockRead
    ): HistoryStatus = when {
        entries.isNotEmpty() && hist.status == HistoryStatus.PARTIAL -> HistoryStatus.PARTIAL
        entries.isNotEmpty() -> HistoryStatus.OK
        hist.status == HistoryStatus.READ_FAILED -> HistoryStatus.READ_FAILED
        hist.status == HistoryStatus.PARTIAL -> HistoryStatus.PARTIAL
        else -> HistoryStatus.EMPTY_CARD
    }

    private fun tryPoll(tx: Transceiver, systemCode: Int): Pair<ByteArray, ByteArray>? {
        return try {
            val poll = tx.transceive(pollingCommand(systemCode))
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

    /**
     * Read [count] blocks starting at [start].
     * Tries [CHUNK]-sized requests, then falls back to single-block reads
     * (the pattern used by nfcpy samples / many phone Suica readers).
     */
    internal fun readServiceRange(
        tx: Transceiver,
        idm: ByteArray,
        service: Int,
        start: Int,
        count: Int
    ): BlockRead {
        val out = ArrayList<ByteArray>(count)
        var i = start
        val end = start + count
        var usedFallback = false
        var hitIoFailure = false
        var lastError: String? = null

        while (i < end) {
            val remaining = end - i
            val n = if (usedFallback) 1 else min(CHUNK, remaining)
            val indices = IntArray(n) { i + it }
            val result = readBlocksResult(tx, idm, service, indices)
            when {
                result.blocks.isNotEmpty() -> {
                    out.addAll(result.blocks)
                    if (result.blocks.size < n) {
                        break
                    }
                    i += result.blocks.size
                }
                n > 1 -> {
                    // Some cards cap blocks per command below CHUNK: retry one at a time.
                    usedFallback = true
                    lastError = result.error ?: lastError
                    continue
                }
                else -> {
                    hitIoFailure = hitIoFailure || result.failed
                    lastError = result.error ?: lastError
                    break
                }
            }
        }

        return when {
            out.isNotEmpty() && i < end && hitIoFailure -> BlockRead(
                blocks = out,
                status = HistoryStatus.PARTIAL,
                message = lastError
                    ?: "Read ${out.size} block(s) then lost the tag — hold still and rescan."
            )
            out.isNotEmpty() -> BlockRead(out, HistoryStatus.OK)
            hitIoFailure -> BlockRead(
                blocks = emptyList(),
                status = HistoryStatus.READ_FAILED,
                message = lastError
                    ?: "History read failed — hold the card still on the NFC spot and try again."
            )
            else -> BlockRead(emptyList(), HistoryStatus.EMPTY_CARD)
        }
    }

    private data class BlocksResult(
        val blocks: List<ByteArray>,
        val failed: Boolean,
        val error: String? = null
    )

    private fun readBlocks(
        tx: Transceiver,
        idm: ByteArray,
        service: Int,
        blocks: IntArray
    ): List<ByteArray> = readBlocksResult(tx, idm, service, blocks).blocks

    private fun readBlocksResult(
        tx: Transceiver,
        idm: ByteArray,
        service: Int,
        blocks: IntArray
    ): BlocksResult {
        return try {
            val resp = tx.transceive(readWithoutEncryption(idm, service, blocks))
            val got = extractBlocks(resp)
            val flags = statusFlags(resp)
            if (got.isEmpty() && flags != null && (flags.first != 0 || flags.second != 0)) {
                // The card answered and refused (e.g. block past the end of the service):
                // not a lost tag, so a single-block refusal just ends the range.
                BlocksResult(
                    emptyList(),
                    failed = false,
                    error = "Card refused read: SF1=%02X SF2=%02X".format(flags.first, flags.second)
                )
            } else if (got.isEmpty()) {
                BlocksResult(emptyList(), failed = true, error = "Empty/invalid FeliCa read response")
            } else {
                BlocksResult(got, failed = false)
            }
        } catch (e: Exception) {
            val tip = if (e.message?.contains("lost", ignoreCase = true) == true) {
                "Tag was lost mid-read — hold still on the NFC spot and try again."
            } else {
                "Read error: ${e.message ?: e.javaClass.simpleName}"
            }
            BlocksResult(emptyList(), failed = true, error = tip)
        }
    }

    fun pollingCommand(systemCode: Int): ByteArray {
        val scHi = ((systemCode shr 8) and 0xff).toByte()
        val scLo = (systemCode and 0xff).toByte()
        return byteArrayOf(0x06, 0x00, scHi, scLo, 0x01, 0x0f)
    }

    /** Request System Code (0x0C) for [idm]. */
    fun requestSystemCodeCommand(idm: ByteArray): ByteArray {
        require(idm.size == 8)
        return byteArrayOf(10, 0x0C) + idm
    }

    /** System codes (big-endian) from a Request System Code response, or null if malformed. */
    fun parseSystemCodes(resp: ByteArray): List<Int>? {
        var off = 0
        if (resp.isNotEmpty() &&
            ((resp[0].toInt() and 0xff) == resp.size || (resp[0].toInt() and 0xff) + 1 == resp.size)
        ) {
            off = 1
        }
        if (off >= resp.size || (resp[off].toInt() and 0xff) != 0x0D) return null
        off += 1 + 8
        if (off >= resp.size) return null
        val n = resp[off].toInt() and 0xff
        off += 1
        if (n == 0 || off + 2 * n > resp.size) return null
        return (0 until n).map {
            ((resp[off + 2 * it].toInt() and 0xff) shl 8) or (resp[off + 2 * it + 1].toInt() and 0xff)
        }
    }

    /** System code from a Polling (request code 1) response, or null if absent. */
    fun parsePollingSystemCode(resp: ByteArray): Int? {
        var off = 0
        if (resp.isNotEmpty() &&
            ((resp[0].toInt() and 0xff) == resp.size || (resp[0].toInt() and 0xff) + 1 == resp.size)
        ) {
            off = 1
        }
        if (off >= resp.size || (resp[off].toInt() and 0xff) != 0x01) return null
        off += 1 + 8 + 8
        if (off + 2 > resp.size) return null
        return ((resp[off].toInt() and 0xff) shl 8) or (resp[off + 1].toInt() and 0xff)
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
        for (b in blocks) blockListElement(b).forEach { out.add(it) }
        out[0] = out.size.toByte()
        return out.toByteArray()
    }

    /**
     * One block-list element for service-code-list order 0, access mode 0
     * (FeliCa Card User's Manual, "Block List"): 2-byte form `0x80, block` for
     * block numbers 0..255, 3-byte form `0x00, lo, hi` above that.
     * D0 low nibble is the service's index in the command's service list,
     * never the block number.
     */
    fun blockListElement(block: Int): ByteArray {
        require(block in 0..0xffff)
        return if (block <= 0xff) {
            byteArrayOf(0x80.toByte(), block.toByte())
        } else {
            byteArrayOf(0x00, (block and 0xff).toByte(), ((block shr 8) and 0xff).toByte())
        }
    }

    /** Status flags (SF1, SF2) of a Read Without Encryption response, or null if malformed. */
    fun statusFlags(resp: ByteArray): Pair<Int, Int>? {
        if (resp.isEmpty()) return null
        var off = 0
        if ((resp[0].toInt() and 0xff) == resp.size || (resp[0].toInt() and 0xff) + 1 == resp.size) {
            off = 1
        }
        if (off >= resp.size || (resp[off].toInt() and 0xff) != 0x07) return null
        off += 1 + 8
        if (off + 2 > resp.size) return null
        return (resp[off].toInt() and 0xff) to (resp[off + 1].toInt() and 0xff)
    }

    fun extractBlocks(resp: ByteArray): List<ByteArray> {
        if (resp.isEmpty()) return emptyList()
        var off = 0
        if ((resp[0].toInt() and 0xff) == resp.size || (resp[0].toInt() and 0xff) + 1 == resp.size) {
            off = 1
        }
        if (off >= resp.size) return emptyList()
        val cmd = resp[off].toInt() and 0xff
        if (cmd != 0x07) return emptyList()
        off += 1
        if (off + 8 + 2 > resp.size) return emptyList()
        off += 8
        val st1 = resp[off].toInt() and 0xff
        val st2 = resp[off + 1].toInt() and 0xff
        off += 2
        if (st1 != 0 || st2 != 0) return emptyList()
        if (off < resp.size && (resp.size - off) % 16 == 1) off += 1
        val blocks = mutableListOf<ByteArray>()
        while (off + 16 <= resp.size) {
            blocks.add(resp.copyOfRange(off, off + 16))
            off += 16
        }
        return blocks
    }
}

package works.squirrel.suicaanalysis

/**
 * FeliCa command builders and response parsers for the analysis tool.
 *
 * Original code written from the public "FeliCa Card User's Manual" command
 * descriptions (frame layouts only). Every frame starts with its own length byte,
 * as Android's NfcF.transceive and nfcpy's clf.exchange expect.
 *
 * Multi-byte codes: system codes are big-endian; service/area codes, block
 * numbers and key versions are little-endian.
 */
object FelicaProtocol {
    const val CMD_POLLING = 0x00
    const val CMD_REQUEST_SERVICE = 0x02
    const val CMD_REQUEST_RESPONSE = 0x04
    const val CMD_READ = 0x06
    const val CMD_SEARCH_SERVICE = 0x0A
    const val CMD_REQUEST_SYSTEM_CODE = 0x0C

    /** Polling request codes. */
    const val RC_NONE = 0x00
    const val RC_SYSTEM_CODE = 0x01
    const val RC_COMM_PERFORMANCE = 0x02

    private fun frame(code: Int, body: List<Byte>): ByteArray {
        val out = ArrayList<Byte>(body.size + 2)
        out.add(0)
        out.add(code.toByte())
        out.addAll(body)
        out[0] = out.size.toByte()
        return out.toByteArray()
    }

    private fun idmBody(idm: ByteArray): List<Byte> {
        require(idm.size == 8) { "IDm must be 8 bytes" }
        return idm.toList()
    }

    private fun le16(v: Int): List<Byte> = listOf((v and 0xff).toByte(), ((v shr 8) and 0xff).toByte())

    fun polling(systemCode: Int, requestCode: Int = RC_NONE, timeSlots: Int = 0): ByteArray =
        frame(
            CMD_POLLING,
            listOf(
                ((systemCode shr 8) and 0xff).toByte(),
                (systemCode and 0xff).toByte(),
                requestCode.toByte(),
                timeSlots.toByte()
            )
        )

    fun requestSystemCode(idm: ByteArray): ByteArray = frame(CMD_REQUEST_SYSTEM_CODE, idmBody(idm))

    fun searchServiceCode(idm: ByteArray, index: Int): ByteArray =
        frame(CMD_SEARCH_SERVICE, idmBody(idm) + le16(index))

    fun requestResponse(idm: ByteArray): ByteArray = frame(CMD_REQUEST_RESPONSE, idmBody(idm))

    /** Request Service: returns each node's key version (0xFFFF = node missing). */
    fun requestService(idm: ByteArray, codes: List<Int>): ByteArray {
        require(codes.size in 1..32)
        return frame(CMD_REQUEST_SERVICE, idmBody(idm) + listOf(codes.size.toByte()) + codes.flatMap { le16(it) })
    }

    /**
     * Block-list element for service-list order 0, access mode 0:
     * `0x80, b` for 0..255, `0x00, lo, hi` above.
     */
    fun blockListElement(block: Int): List<Byte> {
        require(block in 0..0xffff)
        return if (block <= 0xff) {
            listOf(0x80.toByte(), block.toByte())
        } else {
            listOf(0x00, (block and 0xff).toByte(), ((block shr 8) and 0xff).toByte())
        }
    }

    fun readWithoutEncryption(idm: ByteArray, serviceCode: Int, blocks: List<Int>): ByteArray {
        require(blocks.isNotEmpty() && blocks.size <= 15)
        return frame(
            CMD_READ,
            idmBody(idm) + listOf(0x01.toByte()) + le16(serviceCode) +
                listOf(blocks.size.toByte()) + blocks.flatMap { blockListElement(it) }
        )
    }

    // ---- responses ----

    /** Response body after the length byte and response code, checked against [expectedCode]. */
    class Response(val code: Int, val idm: ByteArray, val data: ByteArray)

    fun parse(resp: ByteArray, expectedCode: Int): Response? {
        if (resp.size < 2) return null
        var off = 0
        val first = resp[0].toInt() and 0xff
        if (first == resp.size || first + 1 == resp.size) off = 1
        if (off + 1 + 8 > resp.size) return null
        val code = resp[off].toInt() and 0xff
        if (code != expectedCode) return null
        val idm = resp.copyOfRange(off + 1, off + 9)
        return Response(code, idm, resp.copyOfRange(off + 9, resp.size))
    }

    data class PollingReply(val idm: ByteArray, val pmm: ByteArray, val requestData: Int?)

    fun parsePolling(resp: ByteArray): PollingReply? {
        val r = parse(resp, CMD_POLLING + 1) ?: return null
        if (r.data.size < 8) return null
        val pmm = r.data.copyOfRange(0, 8)
        val rd = if (r.data.size >= 10) {
            ((r.data[8].toInt() and 0xff) shl 8) or (r.data[9].toInt() and 0xff)
        } else {
            null
        }
        return PollingReply(r.idm, pmm, rd)
    }

    fun parseSystemCodes(resp: ByteArray): List<Int>? {
        val r = parse(resp, CMD_REQUEST_SYSTEM_CODE + 1) ?: return null
        if (r.data.isEmpty()) return null
        val n = r.data[0].toInt() and 0xff
        if (r.data.size < 1 + 2 * n) return null
        return (0 until n).map {
            ((r.data[1 + 2 * it].toInt() and 0xff) shl 8) or (r.data[2 + 2 * it].toInt() and 0xff)
        }
    }

    /** One Search Service Code result: a service (end = null), an area, or the end marker. */
    sealed class SearchResult {
        object End : SearchResult()
        data class Service(val code: Int) : SearchResult()
        data class Area(val code: Int, val endCode: Int) : SearchResult()
    }

    fun parseSearchService(resp: ByteArray): SearchResult? {
        val r = parse(resp, CMD_SEARCH_SERVICE + 1) ?: return null
        val d = r.data
        if (d.size < 2) return null
        val code = (d[0].toInt() and 0xff) or ((d[1].toInt() and 0xff) shl 8)
        if (code == 0xffff) return SearchResult.End
        return if (d.size >= 4) {
            SearchResult.Area(code, (d[2].toInt() and 0xff) or ((d[3].toInt() and 0xff) shl 8))
        } else {
            SearchResult.Service(code)
        }
    }

    fun parseKeyVersions(resp: ByteArray): List<Int>? {
        val r = parse(resp, CMD_REQUEST_SERVICE + 1) ?: return null
        if (r.data.isEmpty()) return null
        val n = r.data[0].toInt() and 0xff
        if (r.data.size < 1 + 2 * n) return null
        return (0 until n).map {
            (r.data[1 + 2 * it].toInt() and 0xff) or ((r.data[2 + 2 * it].toInt() and 0xff) shl 8)
        }
    }

    fun parseMode(resp: ByteArray): Int? {
        val r = parse(resp, CMD_REQUEST_RESPONSE + 1) ?: return null
        return r.data.firstOrNull()?.toInt()?.and(0xff)
    }

    data class ReadReply(val sf1: Int, val sf2: Int, val blocks: List<ByteArray>) {
        val ok: Boolean get() = sf1 == 0 && sf2 == 0
    }

    fun parseRead(resp: ByteArray): ReadReply? {
        val r = parse(resp, CMD_READ + 1) ?: return null
        val d = r.data
        if (d.size < 2) return null
        val sf1 = d[0].toInt() and 0xff
        val sf2 = d[1].toInt() and 0xff
        if (sf1 != 0 || sf2 != 0) return ReadReply(sf1, sf2, emptyList())
        if (d.size < 3) return ReadReply(sf1, sf2, emptyList())
        val n = d[2].toInt() and 0xff
        val blocks = (0 until n).mapNotNull { i ->
            val s = 3 + 16 * i
            if (s + 16 <= d.size) d.copyOfRange(s, s + 16) else null
        }
        return ReadReply(sf1, sf2, blocks)
    }

    // ---- service attributes ----

    /** Low 6 bits of a service/area code. */
    fun attribute(code: Int): Int = code and 0x3f

    /** Areas use attribute 0b00000x; everything else is a service. */
    fun isArea(code: Int): Boolean = attribute(code) and 0x3e == 0

    /** Odd attribute = accessible without a key (readable with Read Without Encryption). */
    fun isReadableWithoutKey(code: Int): Boolean = !isArea(code) && attribute(code) and 0x01 == 1

    fun serviceKind(code: Int): String = when (attribute(code) and 0x3e) {
        0x00 -> "area"
        0x08 -> "random-rw"
        0x0a -> "random-ro"
        0x0c -> "cyclic-rw"
        0x0e -> "cyclic-ro"
        0x10 -> "purse-direct"
        0x12 -> "purse-cashback"
        0x14 -> "purse-decrement"
        0x16 -> "purse-ro"
        else -> "unknown"
    }

    // ---- PMm ----

    data class PmmInfo(
        val romType: Int,
        val icType: Int,
        /** Max response time per command class, in ms, for n = 1 (one block/service). */
        val timeoutsMs: Map<String, Double>
    )

    /** Order of the six timeout parameter bytes (PMm bytes 2..7). */
    val PMM_COMMAND_CLASSES = listOf(
        "request_service", "request_response", "authenticate", "read", "write", "other"
    )

    /** One FeliCa time unit T = 256 × 16 / fc (fc = 13.56 MHz) ≈ 0.302 ms. */
    const val T_MS = 256.0 * 16.0 / 13_560_000.0 * 1000.0

    /**
     * Max response time for a parameter byte `EEBBBAAA` and n units:
     * T × ((B + 1) + n × (A + 1)) × 4^E.
     */
    fun responseTimeMs(param: Int, n: Int = 1): Double {
        val a = param and 0x07
        val b = (param shr 3) and 0x07
        val e = (param shr 6) and 0x03
        var pow = 1.0
        repeat(e) { pow *= 4.0 }
        return T_MS * ((b + 1) + n * (a + 1)) * pow
    }

    fun decodePmm(pmm: ByteArray): PmmInfo? {
        if (pmm.size != 8) return null
        val times = LinkedHashMap<String, Double>()
        for ((i, name) in PMM_COMMAND_CLASSES.withIndex()) {
            times[name] = responseTimeMs(pmm[2 + i].toInt() and 0xff)
        }
        return PmmInfo(pmm[0].toInt() and 0xff, pmm[1].toInt() and 0xff, times)
    }
}

fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }

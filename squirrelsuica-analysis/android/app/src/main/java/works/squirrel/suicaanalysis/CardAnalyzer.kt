package works.squirrel.suicaanalysis

import java.io.IOException

/** One FeliCa exchange; NfcF.transceive on Android, a fake in tests. Throws IOException if the tag is lost. */
fun interface Transceiver {
    fun transceive(command: ByteArray): ByteArray
}

data class CommandLog(
    val name: String,
    val command: String,
    val response: String?,
    val millis: Double,
    val error: String? = null
)

data class PollDump(
    val systemCode: Int,
    val requestCode: Int,
    val idm: String?,
    val pmm: String?,
    val requestData: Int?
)

/** One chunk size's run over a service: how far it got and why it stopped. */
data class ReadAttempt(
    val chunkSize: Int,
    val blocksRead: Int,
    /** First block that was not read, or null if it reached [CardAnalyzer.maxBlocks]. */
    val stoppedAt: Int?,
    val sf1: Int?,
    val sf2: Int?,
    val error: String?,
    val commands: Int,
    val avgMillis: Double
)

data class ServiceDump(
    val code: Int,
    val kind: String,
    val readable: Boolean,
    val keyVersion: Int?,
    val blocks: List<String>,
    val attempts: List<ReadAttempt>
)

data class AreaDump(val code: Int, val endCode: Int)

data class SystemDump(
    val systemCode: Int,
    val idm: String?,
    val pmm: String?,
    val mode: Int?,
    val areas: List<AreaDump>,
    val services: List<ServiceDump>,
    val searchStoppedAt: Int?
)

data class CardDump(
    val createdAt: String,
    val tool: String,
    val idm: String?,
    val pmm: String?,
    val polls: List<PollDump>,
    val systemCodes: List<Int>,
    val systems: List<SystemDump>,
    val log: List<CommandLog>,
    val error: String?
)

/**
 * Walks a FeliCa card as far as plain (no-key) commands allow and records
 * everything: polling variants, system codes, every area/service, key versions,
 * and Read Without Encryption at several chunk sizes with status flags and timing.
 */
class CardAnalyzer(
    private val tx: Transceiver,
    private val clockNanos: () -> Long = System::nanoTime,
    val maxBlocks: Int = 64,
    private val maxSearch: Int = 512,
    val chunkSizes: List<Int> = listOf(1, 2, 4, 8, 12, 15)
) {
    private val log = ArrayList<CommandLog>()

    private fun exchange(name: String, cmd: ByteArray): ByteArray? {
        val t0 = clockNanos()
        return try {
            val resp = tx.transceive(cmd)
            log.add(CommandLog(name, cmd.toHex(), resp.toHex(), ms(t0)))
            resp
        } catch (e: IOException) {
            log.add(CommandLog(name, cmd.toHex(), null, ms(t0), e.message ?: e.javaClass.simpleName))
            throw e
        } catch (e: Exception) {
            log.add(CommandLog(name, cmd.toHex(), null, ms(t0), e.message ?: e.javaClass.simpleName))
            null
        }
    }

    private fun ms(t0: Long): Double = (clockNanos() - t0) / 1_000_000.0

    /** Analyse the card. [tagIdm] is the IDm the OS discovered, used if polling fails. */
    fun analyze(tagIdm: ByteArray?, createdAt: String, tool: String): CardDump {
        val polls = ArrayList<PollDump>()
        val systemCodes = ArrayList<Int>()
        val systems = ArrayList<SystemDump>()
        var idm: ByteArray? = tagIdm
        var pmm: ByteArray? = null
        var error: String? = null
        try {
            // Wildcard polling first: every card answers 0xFFFF, and rc=1 names its system 0.
            pollBoth(0xffff, polls)?.let { (i, p) -> idm = i; pmm = p }
            val cardIdm = idm ?: throw IOException("no IDm: card did not answer polling")
            exchange("request system code", FelicaProtocol.requestSystemCode(cardIdm))
                ?.let { FelicaProtocol.parseSystemCodes(it) }
                ?.let { systemCodes.addAll(it) }
            // Only poll system codes the card says it has. A card ignores Polling for a
            // code it lacks, and Android's NfcF reports that silence as a lost tag, which
            // ends the session (nanaco: system 0x04C7 only, died on Polling 0x0003).
            val advertised = systemCodes.ifEmpty {
                listOfNotNull(polls.firstOrNull { it.requestCode == FelicaProtocol.RC_SYSTEM_CODE }?.requestData)
            }
            for (sc in advertised.filter { it != 0xffff }) pollBoth(sc, polls)
            val toWalk = if (systemCodes.isEmpty()) listOf(0xffff) else systemCodes
            for (sc in toWalk) systems.add(walkSystem(sc, cardIdm))
        } catch (e: IOException) {
            error = "Tag lost: ${e.message ?: "I/O error"}"
        }
        return CardDump(
            createdAt = createdAt,
            tool = tool,
            idm = idm?.toHex(),
            pmm = pmm?.toHex(),
            polls = polls,
            systemCodes = systemCodes,
            systems = systems,
            log = log.toList(),
            error = error
        )
    }

    /** Polling [sc] with request codes 1 and 2; returns the first reply's IDm and PMm. */
    private fun pollBoth(sc: Int, polls: MutableList<PollDump>): Pair<ByteArray, ByteArray>? {
        var first: Pair<ByteArray, ByteArray>? = null
        for (rc in listOf(FelicaProtocol.RC_SYSTEM_CODE, FelicaProtocol.RC_COMM_PERFORMANCE)) {
            val resp = exchange("polling %04x rc=%d".format(sc, rc), FelicaProtocol.polling(sc, rc))
            val p = resp?.let { FelicaProtocol.parsePolling(it) }
            polls.add(PollDump(sc, rc, p?.idm?.toHex(), p?.pmm?.toHex(), p?.requestData))
            if (p != null && first == null) first = p.idm to p.pmm
        }
        return first
    }

    private fun walkSystem(systemCode: Int, fallbackIdm: ByteArray): SystemDump {
        // Each system answers with its own IDm; select it by polling its code.
        val p = exchange("polling %04x".format(systemCode), FelicaProtocol.polling(systemCode))
            ?.let { FelicaProtocol.parsePolling(it) }
        val idm = p?.idm ?: fallbackIdm
        val mode = exchange("request response", FelicaProtocol.requestResponse(idm))
            ?.let { FelicaProtocol.parseMode(it) }

        val areas = ArrayList<AreaDump>()
        val serviceCodes = ArrayList<Int>()
        var stoppedAt: Int? = null
        for (i in 0 until maxSearch) {
            val r = exchange("search service #$i", FelicaProtocol.searchServiceCode(idm, i))
                ?.let { FelicaProtocol.parseSearchService(it) }
            when (r) {
                null -> { stoppedAt = i; break }
                FelicaProtocol.SearchResult.End -> break
                is FelicaProtocol.SearchResult.Area -> areas.add(AreaDump(r.code, r.endCode))
                is FelicaProtocol.SearchResult.Service -> serviceCodes.add(r.code)
            }
        }

        val keyVersions = HashMap<Int, Int>()
        for (batch in serviceCodes.chunked(32)) {
            exchange("request service x${batch.size}", FelicaProtocol.requestService(idm, batch))
                ?.let { FelicaProtocol.parseKeyVersions(it) }
                ?.forEachIndexed { i, kv -> if (i < batch.size) keyVersions[batch[i]] = kv }
        }

        val services = serviceCodes.map { code ->
            val readable = FelicaProtocol.isReadableWithoutKey(code)
            var blocks: List<String> = emptyList()
            val attempts = ArrayList<ReadAttempt>()
            if (readable) {
                for (chunk in chunkSizes) {
                    val (attempt, got) = sweep(idm, code, chunk)
                    attempts.add(attempt)
                    if (got.size > blocks.size) blocks = got.map { it.toHex() }
                }
            }
            ServiceDump(code, FelicaProtocol.serviceKind(code), readable, keyVersions[code], blocks, attempts)
        }
        return SystemDump(systemCode, idm.toHex(), p?.pmm?.toHex(), mode, areas, services, stoppedAt)
    }

    /** Read [code] from block 0 in chunks of [chunk] until the card refuses or [maxBlocks]. */
    private fun sweep(idm: ByteArray, code: Int, chunk: Int): Pair<ReadAttempt, List<ByteArray>> {
        val got = ArrayList<ByteArray>()
        var i = 0
        var commands = 0
        var totalMs = 0.0
        var sf1: Int? = null
        var sf2: Int? = null
        var error: String? = null
        var stoppedAt: Int? = null
        while (i < maxBlocks) {
            val n = minOf(chunk, maxBlocks - i)
            val name = "read %04x [%d..%d]".format(code, i, i + n - 1)
            val before = log.size
            val resp = exchange(name, FelicaProtocol.readWithoutEncryption(idm, code, (i until i + n).toList()))
            commands++
            totalMs += log.getOrNull(before)?.millis ?: 0.0
            val r = resp?.let { FelicaProtocol.parseRead(it) }
            if (r == null) {
                error = log.lastOrNull()?.error ?: "invalid response"
                stoppedAt = i
                break
            }
            if (!r.ok) {
                sf1 = r.sf1
                sf2 = r.sf2
                stoppedAt = i
                break
            }
            got.addAll(r.blocks)
            if (r.blocks.size < n) {
                error = "short read: ${r.blocks.size} of $n"
                stoppedAt = i + r.blocks.size
                break
            }
            i += n
        }
        val attempt = ReadAttempt(
            chunkSize = chunk,
            blocksRead = got.size,
            stoppedAt = stoppedAt,
            sf1 = sf1,
            sf2 = sf2,
            error = error,
            commands = commands,
            avgMillis = if (commands > 0) totalMs / commands else 0.0
        )
        return attempt to got
    }
}

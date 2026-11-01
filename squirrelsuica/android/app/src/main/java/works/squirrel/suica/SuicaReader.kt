package works.squirrel.suica

import android.nfc.Tag
import android.nfc.tech.NfcF
import java.io.IOException

/**
 * Read-only Suica/PASMO-class FeliCa access via Android [NfcF].
 *
 * System code 0x0003, balance service 0x090f, history service 0x010f.
 */
object SuicaReader {
    private const val SYSTEM_CODE = 0x0003
    private const val SERVICE_BALANCE = 0x090f
    private const val SERVICE_HISTORY = 0x010f

    fun read(tag: Tag): SuicaCard {
        val nfc = NfcF.get(tag) ?: throw IOException("Not FeliCa / NfcF")
        nfc.connect()
        try {
            val idm = nfc.tag.id ?: throw IOException("missing IDm")
            // Polling for transit system
            val poll = nfc.transceive(pollingCommand(SYSTEM_CODE))
            val pmm = if (poll.size >= 18) poll.copyOfRange(10, 18) else ByteArray(0)

            val balResp = nfc.transceive(readWithoutEncryption(idm, SERVICE_BALANCE, intArrayOf(0)))
            val balBlocks = extractBlocks(balResp)
            if (balBlocks.isEmpty()) throw IOException("no balance block (status?)")
            val balance = SuicaParse.balanceYen(balBlocks[0])

            val history = mutableListOf<ByteArray>()
            for (svc in intArrayOf(SERVICE_HISTORY, SERVICE_BALANCE)) {
                try {
                    val blocks = (0 until 20).toList().toIntArray()
                    val resp = nfc.transceive(readWithoutEncryption(idm, svc, blocks))
                    val got = extractBlocks(resp)
                    if (got.any { it.any { b -> b != 0.toByte() } }) {
                        history.clear()
                        history.addAll(got)
                        break
                    }
                } catch (_: Exception) {
                    // try next service
                }
            }

            return SuicaCard(
                idm = idm.joinToString("") { "%02x".format(it) },
                pmm = pmm.joinToString("") { "%02x".format(it) },
                systemCode = "%04x".format(SYSTEM_CODE),
                balanceYen = balance,
                history = SuicaParse.historyEntries(history)
            )
        } finally {
            try {
                nfc.close()
            } catch (_: Exception) {
            }
        }
    }

    /** FeliCa Polling (command 0x00). */
    fun pollingCommand(systemCode: Int): ByteArray {
        val scHi = ((systemCode shr 8) and 0xff).toByte()
        val scLo = (systemCode and 0xff).toByte()
        // len, cmd, syscode, request code, timeslot
        return byteArrayOf(0x06, 0x00, scHi, scLo, 0x01, 0x0f)
    }

    /**
     * Read Without Encryption (command 0x06).
     * Service code is little-endian on the wire.
     * Block list elements use 2-byte form 0x80|block.
     */
    fun readWithoutEncryption(idm: ByteArray, serviceCode: Int, blocks: IntArray): ByteArray {
        require(idm.size == 8)
        require(blocks.isNotEmpty())
        val out = ArrayList<Byte>(64)
        out.add(0) // length placeholder
        out.add(0x06)
        idm.forEach { out.add(it) }
        out.add(0x01) // number of services
        out.add((serviceCode and 0xff).toByte())
        out.add(((serviceCode shr 8) and 0xff).toByte())
        out.add(blocks.size.toByte())
        for (b in blocks) {
            // 2-byte block list element
            out.add((0x80 or (b and 0xff)).toByte())
            out.add(0x00)
        }
        out[0] = out.size.toByte()
        return out.toByteArray()
    }

    /**
     * Response: len, 0x07, IDm(8), status1, status2, block*16
     * Some stacks omit the leading length in the byte[] returned by transceive.
     */
    fun extractBlocks(resp: ByteArray): List<ByteArray> {
        if (resp.isEmpty()) return emptyList()
        var off = 0
        // If first byte looks like a length matching packet, skip it
        if ((resp[0].toInt() and 0xff) == resp.size || (resp[0].toInt() and 0xff) + 1 == resp.size) {
            off = 1
        }
        if (off >= resp.size) return emptyList()
        val cmd = resp[off].toInt() and 0xff
        if (cmd != 0x07) {
            // Still try to pull trailing 16-byte blocks
            return trailingBlocks(resp)
        }
        off += 1 // response code
        if (off + 8 + 2 > resp.size) return trailingBlocks(resp)
        off += 8 // IDm
        val st1 = resp[off].toInt() and 0xff
        val st2 = resp[off + 1].toInt() and 0xff
        off += 2
        if (st1 != 0 || st2 != 0) {
            throw IOException("FeliCa status %02x %02x".format(st1, st2))
        }
        val blocks = mutableListOf<ByteArray>()
        while (off + 16 <= resp.size) {
            blocks.add(resp.copyOfRange(off, off + 16))
            off += 16
        }
        return blocks.ifEmpty { trailingBlocks(resp) }
    }

    private fun trailingBlocks(resp: ByteArray): List<ByteArray> {
        if (resp.size < 16) return emptyList()
        val n = resp.size / 16
        val start = resp.size - n * 16
        return (0 until n).map { i ->
            val s = start + i * 16
            resp.copyOfRange(s, s + 16)
        }
    }
}

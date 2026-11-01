package works.squirrel.suica

import android.nfc.Tag
import android.nfc.tech.NfcF
import java.io.IOException

/**
 * Read-only Suica/PASMO-class FeliCa access via Android NfcF.
 * System 0x0003, balance service 0x090f.
 */
object SuicaReader {
    private const val SYSTEM_CODE = 0x0003
    private const val SERVICE_BALANCE = 0x090f

    data class Result(
        val idmHex: String,
        val balanceYen: Int,
        val rawBalanceHex: String
    )

    fun read(tag: Tag): Result {
        val nfc = NfcF.get(tag) ?: throw IOException("Not FeliCa / NfcF")
        nfc.connect()
        try {
            val idm = nfc.tag.id
            // Polling for system code 0x0003
            val polling = pollingCommand(SYSTEM_CODE)
            nfc.transceive(polling)

            val readCmd = readWithoutEncryption(idm, SERVICE_BALANCE, 0)
            val resp = nfc.transceive(readCmd)
            // Response: status(2) + block data(16) ... simplified parse
            if (resp.size < 13) throw IOException("short FeliCa response (${resp.size})")
            // Typical: [len?][response code][idm 8][status1][status2][block...]
            // Find 16-byte block near the end.
            val block = resp.copyOfRange(resp.size - 16, resp.size)
            val balance = (block[0].toInt() and 0xff) or ((block[1].toInt() and 0xff) shl 8)
            return Result(
                idmHex = idm.joinToString("") { "%02x".format(it) },
                balanceYen = balance,
                rawBalanceHex = block.joinToString("") { "%02x".format(it) }
            )
        } finally {
            try { nfc.close() } catch (_: Exception) {}
        }
    }

    private fun pollingCommand(systemCode: Int): ByteArray {
        // FeliCa Polling: length, 0x00, systemCode hi/lo, request code, time slot
        val scHi = ((systemCode shr 8) and 0xff).toByte()
        val scLo = (systemCode and 0xff).toByte()
        return byteArrayOf(0x06, 0x00, scHi, scLo, 0x01, 0x0f)
    }

    private fun readWithoutEncryption(idm: ByteArray, serviceCode: Int, block: Int): ByteArray {
        // Read Without Encryption command 0x06
        val svcHi = ((serviceCode shr 8) and 0xff).toByte()
        val svcLo = (serviceCode and 0xff).toByte()
        // Block list element: 2-byte form 0x80 | block
        val blk = (0x80 or (block and 0x0f)).toByte()
        val payload = ArrayList<Byte>()
        payload.add(0) // length placeholder
        payload.add(0x06) // command
        idm.forEach { payload.add(it) }
        payload.add(0x01) // number of services
        payload.add(svcLo) // little-endian service code on wire for many cards
        payload.add(svcHi)
        payload.add(0x01) // number of blocks
        payload.add(blk)
        payload[0] = payload.size.toByte()
        return payload.toByteArray()
    }
}

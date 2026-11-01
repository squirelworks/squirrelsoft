package works.squirrel.suica

import android.nfc.Tag

/** @deprecated Use [FelicaReader]; kept for existing call sites/tests. */
object SuicaReader {
    fun read(tag: Tag): FelicaCard = FelicaReader.read(tag)

    fun pollingCommand(systemCode: Int): ByteArray = FelicaReader.pollingCommand(systemCode)

    fun readWithoutEncryption(idm: ByteArray, serviceCode: Int, blocks: IntArray): ByteArray =
        FelicaReader.readWithoutEncryption(idm, serviceCode, blocks)

    fun extractBlocks(resp: ByteArray): List<ByteArray> = FelicaReader.extractBlocks(resp)
}

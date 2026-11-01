package works.squirrel.suica

/**
 * Pure parsing helpers for Suica/PASMO-class FeliCa blocks (JVM-testable).
 */
object SuicaParse {
    fun balanceYen(block: ByteArray): Int {
        require(block.size >= 2) { "balance block too short" }
        return u16le(block, 0)
    }

    fun historyEntry(block: ByteArray): HistoryEntry? {
        if (block.size < 16) return null
        if (block.all { it == 0.toByte() }) return null

        var year = 2000 + ((block[4].toInt() and 0xff) shr 1)
        var month = (((block[4].toInt() and 1) shl 3) or ((block[5].toInt() and 0xff) shr 5))
        var day = block[5].toInt() and 0x1f
        if (month !in 1..12 || day !in 1..31 || year !in 2000..2099) {
            year = 2000 + bcd(block[4])
            month = bcd(block[5])
            day = bcd(block[6])
        }
        val date = if (year in 2000..2099 && month in 1..12 && day in 1..31) {
            "%04d-%02d-%02d".format(year, month, day)
        } else {
            "????-??-??"
        }

        val process = block[0].toInt() and 0xff
        val (kind, note) = when (process) {
            0x02 -> "charge" to "チャージ"
            0x01, 0x0F, 0x46, 0x49 -> "train" to "入場/出場"
            0x5C, 0xC7, 0xC8 -> "bus" to "バス"
            else -> "other" to "proc=0x%02x".format(process)
        }

        var fare = u16le(block, 10)
        var bal = u16le(block, 12)
        if (fare > 20000) fare = 0
        if (bal > 100000) bal = 0

        return HistoryEntry(
            date = date,
            kind = kind,
            fareYen = fare,
            balanceYen = bal,
            note = note,
            rawHex = block.joinToString("") { "%02x".format(it) }
        )
    }

    fun historyEntries(blocks: List<ByteArray>): List<HistoryEntry> =
        blocks.mapNotNull { historyEntry(it) }

    fun demoCard(): SuicaCard {
        fun packDate(year: Int, month: Int, day: Int): Pair<Int, Int> {
            val y = year - 2000
            val b4 = ((y shl 1) or ((month shr 3) and 1)) and 0xff
            val b5 = (((month and 7) shl 5) or (day and 0x1f)) and 0xff
            return b4 to b5
        }

        fun hist(process: Int, y: Int, m: Int, d: Int, fare: Int, bal: Int): ByteArray {
            val (b4, b5) = packDate(y, m, d)
            return byteArrayOf(
                process.toByte(), 0, 0, 0,
                b4.toByte(), b5.toByte(), 0, 0, 0, 0,
                (fare and 0xff).toByte(), ((fare shr 8) and 0xff).toByte(),
                (bal and 0xff).toByte(), ((bal shr 8) and 0xff).toByte(),
                0, 0
            )
        }

        val balBlock = byteArrayOf(0xB0.toByte(), 0x09, 0, 0) + ByteArray(12)
        return SuicaCard(
            idm = "0101020304050607",
            pmm = "0000000000000000",
            systemCode = "0003",
            balanceYen = balanceYen(balBlock),
            history = historyEntries(
                listOf(
                    hist(0x01, 2026, 10, 28, 168, 2480),
                    hist(0x01, 2026, 10, 27, 210, 2648),
                    hist(0x02, 2026, 10, 26, 0, 2858)
                )
            )
        )
    }

    private fun u16le(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xff) or ((b[off + 1].toInt() and 0xff) shl 8)

    private fun bcd(v: Byte): Int {
        val x = v.toInt() and 0xff
        return ((x shr 4) and 0xf) * 10 + (x and 0xf)
    }
}

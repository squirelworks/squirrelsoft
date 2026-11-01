package works.squirrel.suica

/**
 * Pure, fixture-testable parsers for publicly documented FeliCa purse layouts.
 * Physical cards are not required — unit tests feed canned blocks.
 */
object FelicaParse {

    /** Suica/PASMO family: service 0x090f block0 bytes 0–1 LE uint16. */
    fun suicaBalance090f(block: ByteArray): Int {
        require(block.size >= 2)
        return u16le(block, 0)
    }

    /**
     * Alternate Suica card-info service 0x008b: balance at bytes 11–12 LE.
     * Used when 0x090f is missing or returns an implausible value.
     */
    fun suicaBalance008b(block: ByteArray): Int {
        require(block.size >= 13)
        return u16le(block, 11)
    }

    /**
     * Pick a Suica-family balance from candidate blocks.
     * Uses 0x090f when sane; if 0x008b disagrees by >2× (common mis-parse),
     * prefer the smaller card-info value.
     */
    fun suicaBalance(block090f: ByteArray?, block008b: ByteArray?): Int? {
        val a = block090f?.takeIf { it.size >= 2 }?.let { suicaBalance090f(it) }
        val b = block008b?.takeIf { it.size >= 13 }?.let { suicaBalance008b(it) }
        fun ok(v: Int) = v in 0..50_000
        return when {
            a != null && b != null && ok(a) && ok(b) && a > b * 2 && b > 0 -> b
            a != null && ok(a) -> a
            b != null && ok(b) -> b
            a != null -> a
            else -> b
        }
    }

    /** nanaco: service 0x5597 block0 bytes 0–3 LE uint32. */
    fun nanacoBalance(block: ByteArray): Int {
        require(block.size >= 4)
        return u32le(block, 0).coerceIn(0, 500_000)
    }

    /** Rakuten Edy: service 0x1317 block0 bytes 0–3 LE uint32. */
    fun edyBalance(block: ByteArray): Int {
        require(block.size >= 4)
        return u32le(block, 0).coerceIn(0, 500_000)
    }

    /** WAON: service 0x6817 block0 bytes 0–1 LE uint16. */
    fun waonBalance(block: ByteArray): Int {
        require(block.size >= 2)
        return u16le(block, 0).coerceIn(0, 500_000)
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
        if (fare > 20_000) fare = 0
        if (bal > 100_000) bal = 0

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

    fun brandLabel(brand: FelicaBrand): String = when (brand) {
        FelicaBrand.SUICA_FAMILY -> "Suica / PASMO"
        FelicaBrand.NANACO -> "nanaco"
        FelicaBrand.EDY -> "楽天Edy"
        FelicaBrand.WAON -> "WAON"
        FelicaBrand.UNKNOWN -> "Unknown FeliCa"
    }

    /** Realistic demo balances (~few thousand yen), not inflated samples. */
    fun demoSuica(): FelicaCard {
        val bal090f = byteArrayOf(0xB0.toByte(), 0x09) + ByteArray(14) // ¥2480
        return FelicaCard(
            brand = FelicaBrand.SUICA_FAMILY,
            brandLabel = brandLabel(FelicaBrand.SUICA_FAMILY),
            idm = "0101020304050607",
            pmm = "0000000000000000",
            systemCode = "0003",
            balanceYen = suicaBalance090f(bal090f),
            history = historyEntries(
                listOf(
                    hist(0x01, 2026, 10, 28, 168, 2480),
                    hist(0x01, 2026, 10, 27, 210, 2648),
                    hist(0x02, 2026, 10, 26, 0, 2858)
                )
            ),
            note = "Demo Suica (synthetic)"
        )
    }

    fun demoNanaco(): FelicaCard {
        // ¥5200 as LE u32
        val block = byteArrayOf(0x50, 0x14, 0x00, 0x00) + ByteArray(12)
        return FelicaCard(
            brand = FelicaBrand.NANACO,
            brandLabel = brandLabel(FelicaBrand.NANACO),
            idm = "02fe0004c7000001",
            pmm = "0000000000000000",
            systemCode = "fe00",
            balanceYen = nanacoBalance(block),
            history = emptyList(),
            note = "Demo nanaco (synthetic) — no physical card required"
        )
    }

    fun demoEdy(): FelicaCard {
        val block = byteArrayOf(0x1C, 0x0C, 0x00, 0x00) + ByteArray(12) // ¥3100
        return FelicaCard(
            brand = FelicaBrand.EDY,
            brandLabel = brandLabel(FelicaBrand.EDY),
            idm = "02fe001317000001",
            pmm = "0000000000000000",
            systemCode = "fe00",
            balanceYen = edyBalance(block),
            history = emptyList(),
            note = "Demo Edy (synthetic)"
        )
    }

    fun demoWaon(): FelicaCard {
        val block = byteArrayOf(0xE8.toByte(), 0x11) + ByteArray(14) // ¥4584
        return FelicaCard(
            brand = FelicaBrand.WAON,
            brandLabel = brandLabel(FelicaBrand.WAON),
            idm = "02fe006817000001",
            pmm = "0000000000000000",
            systemCode = "fe00",
            balanceYen = waonBalance(block),
            history = emptyList(),
            note = "Demo WAON (synthetic)"
        )
    }

    private fun hist(process: Int, y: Int, m: Int, d: Int, fare: Int, bal: Int): ByteArray {
        val yy = y - 2000
        val b4 = ((yy shl 1) or ((m shr 3) and 1)) and 0xff
        val b5 = (((m and 7) shl 5) or (d and 0x1f)) and 0xff
        return byteArrayOf(
            process.toByte(), 0, 0, 0,
            b4.toByte(), b5.toByte(), 0, 0, 0, 0,
            (fare and 0xff).toByte(), ((fare shr 8) and 0xff).toByte(),
            (bal and 0xff).toByte(), ((bal shr 8) and 0xff).toByte(),
            0, 0
        )
    }

    private fun u16le(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xff) or ((b[off + 1].toInt() and 0xff) shl 8)

    private fun u32le(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xff) or
            ((b[off + 1].toInt() and 0xff) shl 8) or
            ((b[off + 2].toInt() and 0xff) shl 16) or
            ((b[off + 3].toInt() and 0xff) shl 24)

    private fun bcd(v: Byte): Int {
        val x = v.toInt() and 0xff
        return ((x shr 4) and 0xf) * 10 + (x and 0xf)
    }
}

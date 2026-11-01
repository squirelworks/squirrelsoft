package works.squirrel.suica

/**
 * Pure, fixture-testable parsers for publicly documented FeliCa purse layouts.
 * Physical cards are not required — unit tests feed canned blocks.
 *
 * Suica history (service 0x090f) 16-byte block layout (open reverse-engineering notes):
 * 0 terminal, 1 process, 2 settlement, 3 entry/exit type,
 * 4–5 date (7-bit year from 2000, 4-bit month, 5-bit day),
 * 6–7 enter line/station (rail) or time/bus fields,
 * 8–9 exit line/station (rail),
 * 10–11 balance LE uint16,
 * 12–14 sequence,
 * 15 region (bits 7–6 enter area, 5–4 exit area for private railways).
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

        val terminal = block[0].toInt() and 0xff
        val process = block[1].toInt() and 0xff

        val dateWord = ((block[4].toInt() and 0xff) shl 8) or (block[5].toInt() and 0xff)
        var year = 2000 + (dateWord shr 9)
        var month = (dateWord shr 5) and 0xf
        var day = dateWord and 0x1f
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

        val isProduct = terminal == 0xc7 || terminal == 0xc8
        val isBus = terminal == 0x05 || process in setOf(0x0d, 0x0f, 0x1f, 0x23)
        val isCharge = process == 0x02 || process == 0x48

        val (kind, baseNote) = when {
            isCharge -> "charge" to "チャージ"
            isBus -> "bus" to processLabel(process)
            isProduct || process in setOf(0x46, 0x49, 0x4a, 0x4b, 0xc6, 0xcb) ->
                "sale" to processLabel(process)
            process in setOf(0x01, 0x03, 0x04, 0x05, 0x06, 0x08, 0x13, 0x14, 0x15, 0x84, 0x85) ->
                "train" to processLabel(process)
            else -> "other" to "proc=0x%02x".format(process)
        }

        var enterName: String? = null
        var exitName: String? = null
        var enterCode: String? = null
        var exitCode: String? = null
        var note = baseNote

        if (!isProduct && !isBus) {
            val region = block[15].toInt() and 0xff
            val inLine = block[6].toInt() and 0xff
            val inSta = block[7].toInt() and 0xff
            val outLine = block[8].toInt() and 0xff
            val outSta = block[9].toInt() and 0xff
            if (inLine != 0 || inSta != 0) {
                enterCode = StationDb.formatCode(inLine, inSta)
                enterName = StationDb.resolveRail(region, inLine, inSta, forExit = false)
                    ?.shortLabel()
                    ?: enterCode
            }
            if (!isCharge && (outLine != 0 || outSta != 0)) {
                exitCode = StationDb.formatCode(outLine, outSta)
                exitName = StationDb.resolveRail(region, outLine, outSta, forExit = true)
                    ?.shortLabel()
                    ?: exitCode
            }
            val route = when {
                enterName != null && exitName != null -> "$enterName → $exitName"
                enterName != null -> enterName
                exitName != null -> exitName
                else -> null
            }
            if (route != null) note = "$baseNote · $route"
        } else if (isProduct) {
            val timeWord = ((block[6].toInt() and 0xff) shl 8) or (block[7].toInt() and 0xff)
            val hh = timeWord shr 11
            val mm = (timeWord shr 5) and 0x3f
            if (hh in 0..23 && mm in 0..59) {
                note = "%s · %02d:%02d".format(baseNote, hh, mm)
            }
        }

        var bal = u16le(block, 10)
        if (bal > 100_000) bal = 0

        return HistoryEntry(
            date = date,
            kind = kind,
            fareYen = 0, // filled by historyEntries() from balance deltas
            balanceYen = bal,
            note = note,
            rawHex = block.joinToString("") { "%02x".format(it) },
            enterStation = enterName,
            exitStation = exitName,
            enterCode = enterCode,
            exitCode = exitCode,
            terminal = terminal,
            process = process
        )
    }

    fun historyEntries(blocks: List<ByteArray>): List<HistoryEntry> {
        val parsed = blocks.mapNotNull { historyEntry(it) }
        // History is newest-first: fare ≈ older.balance − this.balance when spending.
        return parsed.mapIndexed { i, e ->
            val older = parsed.getOrNull(i + 1) ?: return@mapIndexed e
            val delta = older.balanceYen - e.balanceYen
            if (delta in 1..20_000) e.copy(fareYen = delta) else e
        }
    }

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
                    // 乃木坂 → 代々木上原 (Tokyo Metro Chiyoda), bal ¥2480
                    histRail(
                        terminal = 0x16,
                        process = 0x01,
                        y = 2026, m = 10, d = 28,
                        inLine = 0xe3, inSta = 0x59,
                        outLine = 0xe3, outSta = 0x5e,
                        bal = 2480,
                        region = 0x00
                    ),
                    // 渋谷 → 新宿 (Yamanote), bal ¥2648
                    histRail(
                        terminal = 0x16,
                        process = 0x01,
                        y = 2026, m = 10, d = 27,
                        inLine = 0x25, inSta = 0x07,
                        outLine = 0x25, outSta = 0x0a,
                        bal = 2648,
                        region = 0x00
                    ),
                    // Charge at 東京 (Tokaido), bal ¥2858
                    histRail(
                        terminal = 0x16,
                        process = 0x02,
                        y = 2026, m = 10, d = 26,
                        inLine = 0x01, inSta = 0x01,
                        outLine = 0, outSta = 0,
                        bal = 2858,
                        region = 0x00
                    )
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

    /** Build a synthetic rail history block with the public 0x090f layout. */
    fun histRail(
        terminal: Int,
        process: Int,
        y: Int,
        m: Int,
        d: Int,
        inLine: Int,
        inSta: Int,
        outLine: Int,
        outSta: Int,
        bal: Int,
        region: Int,
        settlement: Int = 0,
        entryExit: Int = 0
    ): ByteArray {
        val dateWord = (((y - 2000) and 0x7f) shl 9) or ((m and 0xf) shl 5) or (d and 0x1f)
        return byteArrayOf(
            terminal.toByte(),
            process.toByte(),
            settlement.toByte(),
            entryExit.toByte(),
            ((dateWord shr 8) and 0xff).toByte(),
            (dateWord and 0xff).toByte(),
            inLine.toByte(),
            inSta.toByte(),
            outLine.toByte(),
            outSta.toByte(),
            (bal and 0xff).toByte(),
            ((bal shr 8) and 0xff).toByte(),
            0, 0, 0,
            region.toByte()
        )
    }

    private fun processLabel(process: Int): String = when (process) {
        0x01 -> "運賃支払"
        0x02 -> "チャージ"
        0x03 -> "券購"
        0x04 -> "精算"
        0x05 -> "入場精算"
        0x06 -> "窓出"
        0x0d, 0x0f -> "バス"
        0x13 -> "新幹線"
        0x14 -> "入A"
        0x15 -> "出A"
        0x46 -> "物販"
        0x48 -> "特典チャージ"
        else -> "proc=0x%02x".format(process)
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

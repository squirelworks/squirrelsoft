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
    private val SALE_PROCESSES = setOf(0x46, 0x49, 0x4a, 0x4b, 0xc6, 0xcb)
    private val BUS_PROCESSES = setOf(0x0d, 0x0f, 0x1f, 0x23)

    /**
     * Suica/PASMO family: service 0x090f history block balance at bytes 10–11 LE.
     * (Older code wrongly read bytes 0–1 = terminal/process, producing ~10× inflated balances.)
     */
    fun suicaBalance090f(block: ByteArray): Int {
        require(block.size >= 12)
        return u16le(block, 10)
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
     * Prefers a sane 0x090f history-block balance; falls back to 0x008b.
     * If both disagree by >2×, prefer the smaller card-info value (legacy mis-parse guard).
     */
    fun suicaBalance(block090f: ByteArray?, block008b: ByteArray?): Int? {
        val a = block090f?.takeIf { it.size >= 12 }?.let { suicaBalance090f(it) }
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
        val settlement = block[2].toInt() and 0xff

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

        // Shop sales carry a time and terminal ID in bytes 6-9, not stations: terminal
        // types C7/C8, and any non-bus sale process (real dumps show CA with process 46).
        // Bus (terminal 0x05 or bus process): bytes 6-9 are operator + stop, never rail.
        val isBus = terminal == 0x05 || process in BUS_PROCESSES
        val isProduct = terminal == 0xc7 || terminal == 0xc8 || (process in SALE_PROCESSES && !isBus)
        val isCharge = process == 0x02 || process == 0x48

        val (kind, baseNote) = when {
            isCharge -> "charge" to "チャージ"
            isBus -> "bus" to processLabel(process)
            isProduct -> "sale" to processLabel(process)
            process in setOf(0x01, 0x03, 0x04, 0x05, 0x06, 0x08, 0x13, 0x14, 0x15, 0x84, 0x85) ->
                "train" to processLabel(process)
            else -> "other" to "proc=0x%02x".format(process)
        }

        var enterName: String? = null
        var exitName: String? = null
        var enterCode: String? = null
        var exitCode: String? = null
        var busOperator: Int? = null
        var busStop: Int? = null
        var note = baseNote

        if (isBus) {
            busOperator = ((block[6].toInt() and 0xff) shl 8) or (block[7].toInt() and 0xff)
            busStop = ((block[8].toInt() and 0xff) shl 8) or (block[9].toInt() and 0xff)
            note = "%s · 事業者 %04X · 停留所 %04X".format(baseNote, busOperator, busStop)
        } else if (!isProduct) {
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
            process = process,
            settlement = settlement,
            busOperator = busOperator,
            busStop = busStop
        )
    }

    fun historyEntries(blocks: List<ByteArray>): List<HistoryEntry> {
        val parsed = blocks.mapNotNull { historyEntry(it) }
        // History is newest-first:
        // spend fare ≈ older.balance − this.balance; charge amount ≈ this − older.
        return parsed.mapIndexed { i, e ->
            val older = parsed.getOrNull(i + 1) ?: return@mapIndexed e
            val delta = older.balanceYen - e.balanceYen
            when {
                delta in 1..20_000 -> e.copy(fareYen = delta)
                e.kind == "charge" && -delta in 1..50_000 -> e.copy(fareYen = -delta)
                else -> e
            }
        }
    }

    /**
     * nanaco history service 0x564f — 5×16-byte records, newest first.
     * byte0 type; 1–4 amount BE; 5–8 balance BE; 9–12 packed datetime;
     * 13–14 sequence. Types: 0x47 pay, 0x5c/0x6f/0x70 charge, …
     */
    fun nanacoHistoryEntry(block: ByteArray): HistoryEntry? {
        if (block.size < 16) return null
        if (block.all { it == 0.toByte() }) return null
        val type = block[0].toInt() and 0xff
        if (type == 0) return null
        val amount = u32be(block, 1)
        val bal = u32be(block, 5).coerceIn(0, 500_000)
        val packed =
            ((block[9].toLong() and 0xff) shl 24) or
                ((block[10].toLong() and 0xff) shl 16) or
                ((block[11].toLong() and 0xff) shl 8) or
                (block[12].toLong() and 0xff)
        // year(11 from 2000) month(4) day(5) hour(6) minute(6) = 32 bits
        val year = 2000 + ((packed shr 21) and 0x7ff).toInt()
        val month = ((packed shr 17) and 0xf).toInt()
        val day = ((packed shr 12) and 0x1f).toInt()
        val hour = ((packed shr 6) and 0x3f).toInt()
        val minute = (packed and 0x3f).toInt()
        val date = if (year in 2000..2099 && month in 1..12 && day in 1..31) {
            "%04d-%02d-%02d".format(year, month, day)
        } else {
            "????-??-??"
        }
        val timeOk = hour in 0..23 && minute in 0..59
        val (kind, baseNote) = when (type) {
            0x47 -> "sale" to "支払"
            0x5c, 0x6f, 0x70 -> "charge" to "チャージ"
            0x83 -> "other" to "ポイント交換"
            0x35 -> "other" to "転送"
            else -> "other" to "type=0x%02x".format(type)
        }
        val note = if (timeOk) {
            "%s · %02d:%02d · ¥%d".format(baseNote, hour, minute, amount)
        } else {
            "%s · ¥%d".format(baseNote, amount)
        }
        return HistoryEntry(
            date = date,
            kind = kind,
            fareYen = if (kind == "sale" && amount in 1..100_000) amount else 0,
            balanceYen = bal,
            note = note,
            rawHex = block.joinToString("") { "%02x".format(it) },
            terminal = type,
            process = type
        )
    }

    fun nanacoHistoryEntries(blocks: List<ByteArray>): List<HistoryEntry> =
        blocks.mapNotNull { nanacoHistoryEntry(it) }

    /**
     * Edy history service 0x170f — 6×16-byte records.
     * byte0 type (0x20 pay, 0x02 charge, 0x04 gift); 1–3 seq;
     * 4–7 timestamp (15-bit days since 2000-01-01 + 17-bit seconds);
     * 8–11 amount BE; 12–15 balance BE.
     */
    fun edyHistoryEntry(block: ByteArray): HistoryEntry? {
        if (block.size < 16) return null
        if (block.all { it == 0.toByte() }) return null
        val type = block[0].toInt() and 0xff
        if (type == 0) return null
        val ts = u32be(block, 4).toLong() and 0xffffffffL
        val days = (ts shr 17).toInt()
        val seconds = (ts and 0x1ffff).toInt()
        val date = edyDaysToDate(days)
        val hour = seconds / 3600
        val minute = (seconds % 3600) / 60
        val amount = u32be(block, 8)
        val bal = u32be(block, 12).coerceIn(0, 500_000)
        val (kind, baseNote) = when (type) {
            0x20 -> "sale" to "支払"
            0x02 -> "charge" to "チャージ"
            0x04 -> "charge" to "ギフト"
            else -> "other" to "type=0x%02x".format(type)
        }
        val note = "%s · %02d:%02d · ¥%d".format(baseNote, hour.coerceIn(0, 23), minute.coerceIn(0, 59), amount)
        return HistoryEntry(
            date = date,
            kind = kind,
            fareYen = if (kind == "sale" && amount in 1..100_000) amount else 0,
            balanceYen = bal,
            note = note,
            rawHex = block.joinToString("") { "%02x".format(it) },
            terminal = type,
            process = type
        )
    }

    fun edyHistoryEntries(blocks: List<ByteArray>): List<HistoryEntry> =
        blocks.mapNotNull { edyHistoryEntry(it) }

    /**
     * WAON history service 0x680b — three 32-byte records in blocks 0–5.
     * type at byte 17; from byte 18 MSB-first: year5(+2005) month4 day5 hour5
     * minute6 balance18 usage18 charge17.
     */
    fun waonHistoryEntry(record32: ByteArray): HistoryEntry? {
        if (record32.size < 32) return null
        if (record32.all { it == 0.toByte() }) return null
        val type = record32[17].toInt() and 0xff
        if (type == 0) return null
        // Pack bytes 18.. from MSB.
        var bit = 0
        fun take(n: Int): Int {
            var v = 0
            repeat(n) {
                val byteIndex = 18 + (bit / 8)
                val bitIndex = 7 - (bit % 8)
                val b = record32[byteIndex].toInt() and 0xff
                v = (v shl 1) or ((b shr bitIndex) and 1)
                bit++
            }
            return v
        }
        val year = 2005 + take(5)
        val month = take(4)
        val day = take(5)
        val hour = take(5)
        val minute = take(6)
        val bal = take(18).coerceIn(0, 500_000)
        val usage = take(18)
        val charge = take(17)
        val date = if (year in 2005..2099 && month in 1..12 && day in 1..31) {
            "%04d-%02d-%02d".format(year, month, day)
        } else {
            "????-??-??"
        }
        val (kind, baseNote, fare) = when (type) {
            0x04 -> Triple("sale", "支払", usage)
            0x0c, 0x10 -> Triple("charge", "チャージ", charge)
            else -> Triple("other", "type=0x%02x".format(type), usage)
        }
        val note = "%s · %02d:%02d · ¥%d".format(
            baseNote,
            hour.coerceIn(0, 23),
            minute.coerceIn(0, 59),
            fare
        )
        return HistoryEntry(
            date = date,
            kind = kind,
            fareYen = if (kind == "sale" && fare in 1..100_000) fare else 0,
            balanceYen = bal,
            note = note,
            rawHex = record32.joinToString("") { "%02x".format(it) },
            terminal = type,
            process = type
        )
    }

    fun waonHistoryEntries(blocks: List<ByteArray>): List<HistoryEntry> {
        val out = ArrayList<HistoryEntry>(3)
        var i = 0
        while (i + 1 < blocks.size) {
            val rec = blocks[i] + blocks[i + 1]
            waonHistoryEntry(rec)?.let { out.add(it) }
            i += 2
        }
        return out
    }

    fun brandLabel(brand: FelicaBrand): String = when (brand) {
        FelicaBrand.SUICA_FAMILY -> "Suica / PASMO"
        FelicaBrand.NANACO -> "nanaco"
        FelicaBrand.EDY -> "楽天Edy"
        FelicaBrand.WAON -> "WAON"
        FelicaBrand.UNKNOWN -> "Unknown FeliCa"
    }

    /**
     * Transit-IC display name from attributes 0x008B issuer byte and optional history settlement.
     * Public layout: byte 8 = card-type (high nibble) | region (low nibble).
     */
    fun suicaFamilyLabel(attrs008b: ByteArray?, settlement: Int? = null): String {
        val cardType: Int?
        val region: Int?
        if (attrs008b != null && attrs008b.size > 8) {
            val b = attrs008b[8].toInt() and 0xff
            cardType = (b shr 4) and 0xf
            region = b and 0xf
        } else {
            cardType = null
            region = null
        }
        when (cardType) {
            3 -> return "ICOCA"
            0 -> return "IruCa"
        }
        when (settlement) {
            0x0d -> return "PASMO"
            0x3f -> return "モバイルSuica"
            0x0b -> return "PiTaPa"
            0x02 -> return "Suica"
        }
        if (cardType == 2 && region == 2) return "PiTaPa"
        return brandLabel(FelicaBrand.SUICA_FAMILY)
    }

    /** Synthetic attributes 0x008B block for fixtures. */
    fun demoSuicaAttrs(cardType: Int = 2, region: Int = 0, bal: Int = 2480): ByteArray {
        val out = ByteArray(16)
        out[8] = (((cardType and 0xf) shl 4) or (region and 0xf)).toByte()
        out[11] = (bal and 0xff).toByte()
        out[12] = ((bal shr 8) and 0xff).toByte()
        return out
    }

    /** Fixture helpers for unit tests (not exposed in the UI). */
    fun demoSuica(): FelicaCard {
        val hist = historyEntries(
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
                ),
                // Prior balance before charge (for charge amount delta)
                histRail(
                    terminal = 0x16,
                    process = 0x01,
                    y = 2026, m = 10, d = 25,
                    inLine = 0x25, inSta = 0x01,
                    outLine = 0x25, outSta = 0x02,
                    bal = 858,
                    region = 0x00
                )
            )
        )
        val attrs = demoSuicaAttrs(2, 0, 2480)
        return FelicaCard(
            brand = FelicaBrand.SUICA_FAMILY,
            brandLabel = suicaFamilyLabel(attrs, hist.firstOrNull()?.settlement),
            idm = "0101020304050607",
            pmm = "0000000000000000",
            systemCode = "0003",
            balanceYen = 2480,
            history = hist,
            note = "Fixture Suica (synthetic)",
            historyStatus = HistoryStatus.OK
        )
    }

    fun demoNanaco(): FelicaCard {
        val balBlock = byteArrayOf(0x50, 0x14, 0x00, 0x00) + ByteArray(12)
        val hist = listOf(
            histNanaco(type = 0x47, amount = 480, bal = 5200, y = 2026, m = 10, d = 28, hh = 18, mm = 42),
            histNanaco(type = 0x6f, amount = 3000, bal = 5680, y = 2026, m = 10, d = 27, hh = 12, mm = 5)
        )
        return FelicaCard(
            brand = FelicaBrand.NANACO,
            brandLabel = brandLabel(FelicaBrand.NANACO),
            idm = "02fe0004c7000001",
            pmm = "0000000000000000",
            systemCode = "fe00",
            balanceYen = nanacoBalance(balBlock),
            history = nanacoHistoryEntries(hist),
            note = "Fixture nanaco (synthetic)",
            historyStatus = HistoryStatus.OK
        )
    }

    fun demoEdy(): FelicaCard {
        val block = byteArrayOf(0x1C, 0x0C, 0x00, 0x00) + ByteArray(12) // ¥3100
        val hist = listOf(
            histEdy(type = 0x20, amount = 350, bal = 3100, days = 9800, seconds = 14 * 3600 + 22 * 60),
            histEdy(type = 0x02, amount = 2000, bal = 3450, days = 9799, seconds = 10 * 3600)
        )
        return FelicaCard(
            brand = FelicaBrand.EDY,
            brandLabel = brandLabel(FelicaBrand.EDY),
            idm = "02fe001317000001",
            pmm = "0000000000000000",
            systemCode = "fe00",
            balanceYen = edyBalance(block),
            history = edyHistoryEntries(hist),
            note = "Fixture Edy (synthetic)",
            historyStatus = HistoryStatus.OK
        )
    }

    fun demoWaon(): FelicaCard {
        val block = byteArrayOf(0xE8.toByte(), 0x11) + ByteArray(14) // ¥4584
        val rec = histWaon(
            type = 0x04,
            y = 2026, m = 10, d = 28, hh = 19, mm = 10,
            bal = 4584, usage = 620, charge = 0
        )
        return FelicaCard(
            brand = FelicaBrand.WAON,
            brandLabel = brandLabel(FelicaBrand.WAON),
            idm = "02fe006817000001",
            pmm = "0000000000000000",
            systemCode = "fe00",
            balanceYen = waonBalance(block),
            history = waonHistoryEntries(listOf(rec.copyOfRange(0, 16), rec.copyOfRange(16, 32))),
            note = "Fixture WAON (synthetic)",
            historyStatus = HistoryStatus.OK
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

    fun histNanaco(
        type: Int,
        amount: Int,
        bal: Int,
        y: Int,
        m: Int,
        d: Int,
        hh: Int,
        mm: Int
    ): ByteArray {
        val packed =
            (((y - 2000) and 0x7ff) shl 21) or
                ((m and 0xf) shl 17) or
                ((d and 0x1f) shl 12) or
                ((hh and 0x3f) shl 6) or
                (mm and 0x3f)
        val out = ByteArray(16)
        out[0] = type.toByte()
        putU32be(out, 1, amount)
        putU32be(out, 5, bal)
        out[9] = ((packed shr 24) and 0xff).toByte()
        out[10] = ((packed shr 16) and 0xff).toByte()
        out[11] = ((packed shr 8) and 0xff).toByte()
        out[12] = (packed and 0xff).toByte()
        return out
    }

    fun histEdy(type: Int, amount: Int, bal: Int, days: Int, seconds: Int): ByteArray {
        val out = ByteArray(16)
        out[0] = type.toByte()
        val ts = ((days and 0x7fff) shl 17) or (seconds and 0x1ffff)
        putU32be(out, 4, ts)
        putU32be(out, 8, amount)
        putU32be(out, 12, bal)
        return out
    }

    fun histWaon(
        type: Int,
        y: Int,
        m: Int,
        d: Int,
        hh: Int,
        mm: Int,
        bal: Int,
        usage: Int,
        charge: Int
    ): ByteArray {
        val out = ByteArray(32)
        out[17] = type.toByte()
        val fields = intArrayOf(
            (y - 2005) and 0x1f, 5,
            m and 0xf, 4,
            d and 0x1f, 5,
            hh and 0x1f, 5,
            mm and 0x3f, 6,
            bal and 0x3ffff, 18,
            usage and 0x3ffff, 18,
            charge and 0x1ffff, 17
        )
        var bit = 0
        var i = 0
        while (i < fields.size) {
            val value = fields[i]
            val width = fields[i + 1]
            for (b in (width - 1) downTo 0) {
                val byteIndex = 18 + (bit / 8)
                val bitIndex = 7 - (bit % 8)
                if (((value shr b) and 1) == 1) {
                    out[byteIndex] = (out[byteIndex].toInt() or (1 shl bitIndex)).toByte()
                }
                bit++
            }
            i += 2
        }
        return out
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
        0x1f -> "バスチャージ"
        0x23 -> "バス券購"
        0x46 -> "物販"
        0x48 -> "特典チャージ"
        else -> "proc=0x%02x".format(process)
    }

    private fun edyDaysToDate(days: Int): String {
        // Approximate civil date from days since 2000-01-01 (no TZ).
        if (days < 0 || days > 40000) return "????-??-??"
        var rem = days
        var year = 2000
        while (true) {
            val diy = if (isLeap(year)) 366 else 365
            if (rem < diy) break
            rem -= diy
            year++
        }
        val md = if (isLeap(year)) {
            intArrayOf(31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
        } else {
            intArrayOf(31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
        }
        var month = 1
        for (len in md) {
            if (rem < len) break
            rem -= len
            month++
        }
        val day = rem + 1
        return "%04d-%02d-%02d".format(year, month, day)
    }

    private fun isLeap(y: Int): Boolean =
        (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)

    private fun u16le(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xff) or ((b[off + 1].toInt() and 0xff) shl 8)

    private fun u32le(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xff) or
            ((b[off + 1].toInt() and 0xff) shl 8) or
            ((b[off + 2].toInt() and 0xff) shl 16) or
            ((b[off + 3].toInt() and 0xff) shl 24)

    private fun u32be(b: ByteArray, off: Int): Int =
        ((b[off].toInt() and 0xff) shl 24) or
            ((b[off + 1].toInt() and 0xff) shl 16) or
            ((b[off + 2].toInt() and 0xff) shl 8) or
            (b[off + 3].toInt() and 0xff)

    private fun putU32be(b: ByteArray, off: Int, v: Int) {
        b[off] = ((v ushr 24) and 0xff).toByte()
        b[off + 1] = ((v ushr 16) and 0xff).toByte()
        b[off + 2] = ((v ushr 8) and 0xff).toByte()
        b[off + 3] = (v and 0xff).toByte()
    }

    private fun bcd(v: Byte): Int {
        val x = v.toInt() and 0xff
        return ((x shr 4) and 0xf) * 10 + (x and 0xf)
    }
}

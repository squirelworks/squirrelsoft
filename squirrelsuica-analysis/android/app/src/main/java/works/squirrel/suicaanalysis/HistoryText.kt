package works.squirrel.suicaanalysis

/**
 * The History tab: balances and history parsed from the dump's raw blocks by the
 * shared C core. Station names are not resolved here; rail entries show the raw
 * line/station codes, which is what a parser fix needs anyway.
 */
object HistoryText {
    const val SVC_SUICA_HIST = 0x090f
    const val SVC_SUICA_INFO = 0x008b
    const val SVC_NANACO_BAL = 0x5597
    const val SVC_NANACO_HIST = 0x564f
    const val SVC_EDY_BAL = 0x1317
    const val SVC_EDY_HIST = 0x170f
    const val SVC_WAON_BAL = 0x6817

    fun interface Parser {
        fun history(brand: SqFelica.Brand, blocks: List<ByteArray>): List<SqFelica.Entry>?
    }

    fun interface Balancer {
        fun balance(brand: SqFelica.Brand, a: ByteArray?, b: ByteArray?): Int?
    }

    /** Raw blocks of the first readable service with this code, across all systems. */
    fun blocksOf(d: CardDump, code: Int): List<ByteArray>? =
        d.systems.asSequence().flatMap { it.services.asSequence() }
            .firstOrNull { it.code == code && it.readable && it.blocks.isNotEmpty() }
            ?.blocks?.mapNotNull { DumpJson.hexToBytes(it) }

    fun render(
        d: CardDump,
        parser: Parser = Parser { b, x -> SqFelica.history(b, x) },
        balancer: Balancer = Balancer { b, a, c -> SqFelica.balance(b, a, c) }
    ): String = buildString {
        var any = false
        fun section(title: String, brand: SqFelica.Brand, histCode: Int?, balA: ByteArray?, balB: ByteArray?) {
            val hist = histCode?.let { blocksOf(d, it) }
            if (hist == null && balA == null && balB == null) return
            any = true
            appendLine("== $title")
            val bal = balancer.balance(brand, balA, balB)
            when {
                bal == null -> appendLine("   (parser not available in this build)")
                bal >= 0 -> appendLine("   Balance ¥%,d".format(bal))
                else -> appendLine("   Balance: not readable")
            }
            if (hist != null) {
                val rows = parser.history(brand, hist)
                when {
                    rows == null -> {}
                    rows.isEmpty() -> appendLine("   No history entries in ${hist.size} block(s)")
                    else -> rows.forEachIndexed { i, e -> appendLine("   %02d %s".format(i, line(e))) }
                }
            }
            appendLine()
        }
        val suicaHist = blocksOf(d, SVC_SUICA_HIST)
        section(
            "Suica / PASMO (svc 090f)", SqFelica.Brand.SUICA, SVC_SUICA_HIST,
            suicaHist?.firstOrNull(), blocksOf(d, SVC_SUICA_INFO)?.firstOrNull()
        )
        section("nanaco (svc 564f)", SqFelica.Brand.NANACO, SVC_NANACO_HIST, blocksOf(d, SVC_NANACO_BAL)?.firstOrNull(), null)
        section("Edy (svc 170f)", SqFelica.Brand.EDY, SVC_EDY_HIST, blocksOf(d, SVC_EDY_BAL)?.firstOrNull(), null)
        section("WAON (svc 6817)", SqFelica.Brand.WAON, null, blocksOf(d, SVC_WAON_BAL)?.firstOrNull(), null)
        if (!any) appendLine("No Suica, nanaco, Edy or WAON services were readable on this card.")
    }

    fun line(e: SqFelica.Entry): String {
        val date = if (e.year > 0) "%04d-%02d-%02d".format(e.year, e.month, e.day) else "????-??-??"
        val time = if (e.hour >= 0) " %02d:%02d".format(e.hour, e.minute) else "      "
        val fare = when {
            e.fareYen == 0 -> ""
            e.kind == "charge" -> "+¥%,d".format(e.fareYen)
            else -> "-¥%,d".format(e.fareYen)
        }
        val route = listOfNotNull(e.enter, e.exit)
            .joinToString(" → ") { (l, s) -> "%02X/%02X".format(l, s) }
        return buildString {
            append("%s%s %-6s %8s  bal ¥%,d".format(date, time, e.kind, fare, e.balanceYen))
            append("  t%02x p%02x".format(e.terminal, e.process))
            if (route.isNotEmpty()) append("  $route")
        }.trimEnd()
    }
}

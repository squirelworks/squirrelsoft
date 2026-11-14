package works.squirrel.suica

enum class FelicaBrand {
    SUICA_FAMILY, // Suica / PASMO / ICOCA / …
    NANACO,
    EDY,
    WAON,
    UNKNOWN
}

/** Outcome of reading the purse history service (distinct from parse success). */
enum class HistoryStatus {
    /** One or more history rows parsed. */
    OK,
    /** Service responded but no usable records (new/blank card). */
    EMPTY_CARD,
    /** Could not read history blocks (tag lost, IO, wrong service). */
    READ_FAILED,
    /** Some blocks arrived before the tag was lost. */
    PARTIAL
}

data class HistoryEntry(
    val date: String,
    val kind: String,
    val fareYen: Int,
    val balanceYen: Int,
    val note: String,
    val rawHex: String,
    val enterStation: String? = null,
    val exitStation: String? = null,
    val enterCode: String? = null,
    val exitCode: String? = null,
    val terminal: Int = 0,
    val process: Int = 0,
    val settlement: Int = 0,
    val busOperator: Int? = null,
    val busStop: Int? = null
) {
    /** One-line station summary for UI lists. */
    fun stationSummary(): String {
        val enter = enterStation
        val exit = exitStation
        return when {
            !enter.isNullOrBlank() && !exit.isNullOrBlank() && enter != exit ->
                "$enter → $exit"
            !enter.isNullOrBlank() -> enter
            !exit.isNullOrBlank() -> exit
            else -> ""
        }
    }

    /** Bus operator/stop codes (never looked up as rail stations). */
    fun busSummary(): String {
        if (busOperator == null && busStop == null) return ""
        val op = busOperator?.let { "%04X".format(it) } ?: "----"
        val st = busStop?.let { "%04X".format(it) } ?: "----"
        return "事業者 $op · 停留所 $st"
    }
}

/**
 * Read-only snapshot of a FeliCa purse.
 * [balanceYen] is null when the purse was seen but balance could not be parsed.
 */
data class FelicaCard(
    val brand: FelicaBrand,
    val brandLabel: String,
    val idm: String,
    val pmm: String,
    val systemCode: String,
    val balanceYen: Int?,
    val history: List<HistoryEntry>,
    val note: String = "",
    val historyStatus: HistoryStatus = if (history.isNotEmpty()) HistoryStatus.OK else HistoryStatus.EMPTY_CARD,
    val historyMessage: String = ""
)

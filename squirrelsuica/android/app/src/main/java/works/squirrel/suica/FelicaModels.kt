package works.squirrel.suica

enum class FelicaBrand {
    SUICA_FAMILY, // Suica / PASMO / ICOCA / …
    NANACO,
    EDY,
    WAON,
    UNKNOWN
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
    val process: Int = 0
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
    val note: String = ""
)

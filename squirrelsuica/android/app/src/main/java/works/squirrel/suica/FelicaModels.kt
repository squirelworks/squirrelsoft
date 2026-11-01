package works.squirrel.suica

data class HistoryEntry(
    val date: String,
    val kind: String,
    val fareYen: Int,
    val balanceYen: Int,
    val note: String,
    val rawHex: String
)

data class SuicaCard(
    val idm: String,
    val pmm: String,
    val systemCode: String,
    val balanceYen: Int,
    val history: List<HistoryEntry>
)

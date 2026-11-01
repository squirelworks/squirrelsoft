package works.squirrel.suica

/**
 * Compatibility wrappers around [FelicaParse] for Suica-family fixtures.
 */
object SuicaParse {
    fun balanceYen(block: ByteArray): Int = FelicaParse.suicaBalance090f(block)

    fun historyEntry(block: ByteArray): HistoryEntry? = FelicaParse.historyEntry(block)

    fun historyEntries(blocks: List<ByteArray>): List<HistoryEntry> =
        FelicaParse.historyEntries(blocks)

    fun demoCard(): FelicaCard = FelicaParse.demoSuica()
}

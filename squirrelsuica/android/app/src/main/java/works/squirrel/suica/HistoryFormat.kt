package works.squirrel.suica

/**
 * Clean-room history list formatting modeled on typical SuicaReader-style rows:
 * date + process, station route, fare, running balance — not a verbatim UI copy.
 */
object HistoryFormat {

    fun formatEntries(entries: List<HistoryEntry>): String {
        if (entries.isEmpty()) return ""
        return entries.mapIndexed { i, h -> formatEntry(i + 1, h) }.joinToString("\n\n")
    }

    fun formatEntry(index: Int, h: HistoryEntry): String {
        val title = processTitle(h)
        val route = h.stationSummary()
        val fare = when {
            h.kind == "charge" && h.fareYen <= 0 -> "チャージ"
            h.fareYen > 0 && h.kind == "charge" -> "+¥%,d".format(h.fareYen)
            h.fareYen > 0 -> "運賃 ¥%,d".format(h.fareYen)
            else -> "運賃 —"
        }
        val bal = "残高 ¥%,d".format(h.balanceYen)
        val lines = ArrayList<String>(4)
        lines.add("%2d. %s  %s".format(index, h.date, title))
        if (route.isNotEmpty()) lines.add("    $route")
        lines.add("    $fare    $bal")
        val extra = h.note.trim()
        if (extra.isNotEmpty() &&
            !extra.startsWith(title) &&
            (route.isEmpty() || !extra.contains(route))
        ) {
            lines.add("    $extra")
        }
        return lines.joinToString("\n")
    }

    fun formatHistoryPanel(card: FelicaCard): String {
        if (card.history.isNotEmpty()) {
            val body = formatEntries(card.history)
            return when (card.historyStatus) {
                HistoryStatus.PARTIAL ->
                    body + "\n\n—\n" + (
                        card.historyMessage.ifBlank {
                            "Partial history — hold still and scan again for the rest."
                        }
                        )
                else -> body
            }
        }
        return emptyMessage(card.historyStatus, card.historyMessage, card.brand)
    }

    fun emptyMessage(
        status: HistoryStatus,
        message: String = "",
        brand: FelicaBrand = FelicaBrand.UNKNOWN
    ): String {
        val tip = message.trim()
        return when (status) {
            HistoryStatus.OK -> tip.ifBlank { "(no rows)" }
            HistoryStatus.EMPTY_CARD -> tip.ifBlank {
                when (brand) {
                    FelicaBrand.SUICA_FAMILY ->
                        "No journey records on this card yet.\n" +
                            "Ride or charge once, then scan again."
                    else -> "No history records on this purse yet."
                }
            }
            HistoryStatus.READ_FAILED -> tip.ifBlank {
                "Could not read history blocks.\n" +
                    "Hold the card still on the NFC spot and try again.\n" +
                    "(Balance above may still be valid.)"
            }
            HistoryStatus.PARTIAL -> tip.ifBlank {
                "History read was interrupted.\n" +
                    "Hold still and scan again."
            }
        }
    }

    private fun processTitle(h: HistoryEntry): String {
        val fromNote = h.note.substringBefore(" · ").trim()
        if (fromNote.isNotEmpty() && !fromNote.startsWith("proc=") && !fromNote.startsWith("type=")) {
            return fromNote
        }
        return when (h.kind) {
            "train" -> "運賃支払"
            "charge" -> "チャージ"
            "bus" -> "バス"
            "sale" -> "物販"
            else -> h.kind
        }
    }
}

package works.squirrel.suicaanalysis

/** Plain-text views of a [CardDump] for the on-screen Summary and Command log tabs. */
object DumpText {
    fun summary(d: CardDump): String = buildString {
        d.error?.let { appendLine("!! $it"); appendLine() }
        appendLine("IDm  ${d.idm ?: "?"}")
        appendLine("PMm  ${d.pmm ?: "?"}")
        d.pmm?.let { DumpJson.hexToBytes(it) }?.let { FelicaProtocol.decodePmm(it) }?.let { p ->
            appendLine("     ROM type %02x  IC type %02x".format(p.romType, p.icType))
            for ((k, v) in p.timeoutsMs) appendLine("     max %-17s %7.2f ms".format(k, v))
        }
        appendLine()
        appendLine("Polling")
        for (p in d.polls) {
            appendLine(
                "  %04x rc=%d  %s%s".format(
                    p.systemCode, p.requestCode,
                    if (p.idm != null) "answered" else "no answer",
                    p.requestData?.let { "  data=%04x".format(it) } ?: ""
                )
            )
        }
        appendLine()
        appendLine("System codes: " + d.systemCodes.joinToString(" ") { "%04x".format(it) }.ifEmpty { "(none)" })
        for (s in d.systems) {
            appendLine()
            appendLine("== System %04x  IDm %s  mode %s".format(s.systemCode, s.idm ?: "?", s.mode?.toString() ?: "?"))
            if (s.searchStoppedAt != null) appendLine("   service search stopped at #${s.searchStoppedAt}")
            for (a in s.areas) appendLine("   area %04x..%04x".format(a.code, a.endCode))
            for (sv in s.services) {
                val kv = sv.keyVersion?.let { " kv=%04x".format(it) } ?: ""
                if (!sv.readable) {
                    appendLine("   svc %04x %-15s key required%s".format(sv.code, sv.kind, kv))
                    continue
                }
                appendLine("   svc %04x %-15s %d block(s)%s".format(sv.code, sv.kind, sv.blocks.size, kv))
                for (a in sv.attempts) {
                    val why = when {
                        a.sf1 != null -> "SF %02x/%02x".format(a.sf1, a.sf2 ?: 0)
                        a.error != null -> a.error
                        else -> "limit"
                    }
                    appendLine(
                        "      chunk %2d: %2d read, stop@%s (%s), %d cmd, %.1f ms avg".format(
                            a.chunkSize, a.blocksRead, a.stoppedAt?.toString() ?: "-", why, a.commands, a.avgMillis
                        )
                    )
                }
                for ((i, b) in sv.blocks.withIndex()) appendLine("      %02d %s".format(i, b))
            }
        }
    }

    fun log(d: CardDump): String = buildString {
        for ((i, c) in d.log.withIndex()) {
            appendLine("%04d %-28s %6.1f ms".format(i, c.name, c.millis))
            appendLine("  > ${c.command}")
            appendLine("  < ${c.response ?: "(${c.error ?: "no response"})"}")
        }
    }
}

package works.squirrel.suicaanalysis

/**
 * Serialises a [CardDump] to the shared JSON format "squirrel-felica-dump/1"
 * (see ../../schema/felica-dump.schema.json; the desktop twin suica_analyze.py
 * writes the same shape). No Android or third-party JSON dependency.
 */
object DumpJson {
    const val SCHEMA = "squirrel-felica-dump/1"
    const val REDACTED = "redacted"

    fun toJson(dump: CardDump, redactIdm: Boolean = false): String {
        val sb = StringBuilder()
        write(sb, toTree(dump, redactIdm), 0)
        sb.append('\n')
        return sb.toString()
    }

    /** Hex code as "0x090f" so codes stay readable in the dump. */
    fun code4(v: Int): String = "0x%04x".format(v)

    fun toTree(d: CardDump, redactIdm: Boolean): Map<String, Any?> {
        fun idm(v: String?): String? = if (redactIdm && v != null) REDACTED else v
        val allIdms = (listOf(d.idm) + d.polls.map { it.idm } + d.systems.map { it.idm })
            .filterNotNull().filter { it.isNotEmpty() }.toSet()
        val pmmInfo = d.pmm?.let { hexToBytes(it) }?.let { FelicaProtocol.decodePmm(it) }
        return linkedMapOf(
            "schema" to SCHEMA,
            "tool" to d.tool,
            "created_at" to d.createdAt,
            "idm" to idm(d.idm),
            "pmm" to d.pmm,
            "pmm_decoded" to pmmInfo?.let {
                linkedMapOf(
                    "rom_type" to it.romType,
                    "ic_type" to it.icType,
                    "max_response_ms" to it.timeoutsMs.mapValues { (_, v) -> round3(v) }
                )
            },
            "polls" to d.polls.map {
                linkedMapOf(
                    "system_code" to code4(it.systemCode),
                    "request_code" to it.requestCode,
                    "idm" to idm(it.idm),
                    "pmm" to it.pmm,
                    "request_data" to it.requestData?.let { rd -> code4(rd) }
                )
            },
            "system_codes" to d.systemCodes.map { code4(it) },
            "systems" to d.systems.map { s ->
                linkedMapOf(
                    "system_code" to code4(s.systemCode),
                    "idm" to idm(s.idm),
                    "pmm" to s.pmm,
                    "mode" to s.mode,
                    "search_stopped_at" to s.searchStoppedAt,
                    "areas" to s.areas.map { a ->
                        linkedMapOf("code" to code4(a.code), "end_code" to code4(a.endCode))
                    },
                    "services" to s.services.map { sv ->
                        linkedMapOf(
                            "code" to code4(sv.code),
                            "kind" to sv.kind,
                            "readable" to sv.readable,
                            "key_version" to sv.keyVersion?.let { code4(it) },
                            "blocks" to sv.blocks,
                            "attempts" to sv.attempts.map { a ->
                                linkedMapOf(
                                    "chunk_size" to a.chunkSize,
                                    "blocks_read" to a.blocksRead,
                                    "stopped_at" to a.stoppedAt,
                                    "sf1" to a.sf1,
                                    "sf2" to a.sf2,
                                    "error" to a.error,
                                    "commands" to a.commands,
                                    "avg_ms" to round3(a.avgMillis)
                                )
                            }
                        )
                    }
                )
            },
            "log" to d.log.map {
                linkedMapOf(
                    "name" to it.name,
                    "command" to redactHex(it.command, allIdms, redactIdm),
                    "response" to it.response?.let { r -> redactHex(r, allIdms, redactIdm) },
                    "ms" to round3(it.millis),
                    "error" to it.error
                )
            },
            "error" to d.error
        )
    }

    /** Replace every IDm the card used inside raw frames too, when redacting. */
    private fun redactHex(hex: String, idms: Set<String>, redact: Boolean): String {
        if (!redact) return hex
        var out = hex
        for (idm in idms) out = out.replace(idm, "x".repeat(idm.length))
        return out
    }

    private fun round3(v: Double): Double = Math.round(v * 1000.0) / 1000.0

    fun hexToBytes(hex: String): ByteArray? {
        if (hex.length % 2 != 0) return null
        return try {
            ByteArray(hex.length / 2) { hex.substring(2 * it, 2 * it + 2).toInt(16).toByte() }
        } catch (_: NumberFormatException) {
            null
        }
    }

    private fun write(sb: StringBuilder, v: Any?, indent: Int) {
        when (v) {
            null -> sb.append("null")
            is String -> quote(sb, v)
            is Boolean -> sb.append(v)
            is Int, is Long -> sb.append(v)
            is Double -> sb.append(if (v.isFinite()) v.toString() else "null")
            is Map<*, *> -> {
                if (v.isEmpty()) { sb.append("{}"); return }
                sb.append("{\n")
                var first = true
                for ((k, x) in v) {
                    if (!first) sb.append(",\n")
                    first = false
                    pad(sb, indent + 1)
                    quote(sb, k.toString())
                    sb.append(": ")
                    write(sb, x, indent + 1)
                }
                sb.append('\n')
                pad(sb, indent)
                sb.append('}')
            }
            is List<*> -> {
                if (v.isEmpty()) { sb.append("[]"); return }
                sb.append("[\n")
                for ((i, x) in v.withIndex()) {
                    if (i > 0) sb.append(",\n")
                    pad(sb, indent + 1)
                    write(sb, x, indent + 1)
                }
                sb.append('\n')
                pad(sb, indent)
                sb.append(']')
            }
            else -> quote(sb, v.toString())
        }
    }

    private fun pad(sb: StringBuilder, n: Int) {
        repeat(n) { sb.append("  ") }
    }

    private fun quote(sb: StringBuilder, s: String) {
        sb.append('"')
        for (c in s) {
            when {
                c == '"' -> sb.append("\\\"")
                c == '\\' -> sb.append("\\\\")
                c == '\n' -> sb.append("\\n")
                c == '\r' -> sb.append("\\r")
                c == '\t' -> sb.append("\\t")
                c < ' ' -> sb.append("\\u%04x".format(c.code))
                else -> sb.append(c)
            }
        }
        sb.append('"')
    }
}

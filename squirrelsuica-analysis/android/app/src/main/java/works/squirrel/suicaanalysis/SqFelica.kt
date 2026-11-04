package works.squirrel.suicaanalysis

/**
 * Kotlin side of the JNI bridge to the shared C FeliCa core (libs/sqfelica).
 *
 * The native history call returns [FIELDS] ints per entry, in this order:
 * kind, year, month, day, hour, minute, fare, balance, amount, terminal,
 * process, hasEnter, enterLine, enterStation, hasExit, exitLine, exitStation,
 * region. src/main/cpp/sqfelica_jni.c writes them; keep the two in step.
 */
object SqFelica {
    enum class Brand(val id: Int) { SUICA(0), NANACO(1), EDY(2), WAON(3) }

    const val FIELDS = 18
    private val KINDS = listOf("train", "bus", "sale", "charge", "other")

    data class Entry(
        val kind: String,
        val year: Int,
        val month: Int,
        val day: Int,
        /** -1 when the record has no time. */
        val hour: Int,
        val minute: Int,
        val fareYen: Int,
        val balanceYen: Int,
        val amountYen: Int,
        val terminal: Int,
        val process: Int,
        val enter: Pair<Int, Int>?,
        val exit: Pair<Int, Int>?,
        val region: Int
    )

    /** False when the native library is missing (e.g. JVM unit tests). */
    val available: Boolean by lazy {
        try {
            System.loadLibrary("sqfelica_jni")
            true
        } catch (_: UnsatisfiedLinkError) {
            false
        }
    }

    @JvmStatic
    private external fun nativeHistory(brand: Int, blocks: ByteArray): IntArray

    @JvmStatic
    private external fun nativeBalance(brand: Int, a: ByteArray?, b: ByteArray?): Int

    /** Parses history blocks (newest first), or returns null if the native core isn't loaded. */
    fun history(brand: Brand, blocks: List<ByteArray>): List<Entry>? {
        if (!available) return null
        val flat = ByteArray(blocks.size * 16)
        for ((i, b) in blocks.withIndex()) b.copyInto(flat, i * 16, 0, minOf(16, b.size))
        return decode(nativeHistory(brand.id, flat))
    }

    /** Balance in yen, -1 if the needed block is missing, or null if the native core isn't loaded. */
    fun balance(brand: Brand, a: ByteArray?, b: ByteArray? = null): Int? =
        if (available) nativeBalance(brand.id, a, b) else null

    fun decode(v: IntArray): List<Entry> {
        require(v.size % FIELDS == 0) { "history array size ${v.size} is not a multiple of $FIELDS" }
        return (0 until v.size / FIELDS).map { i ->
            val o = i * FIELDS
            Entry(
                kind = KINDS.getOrElse(v[o]) { "other" },
                year = v[o + 1], month = v[o + 2], day = v[o + 3],
                hour = v[o + 4], minute = v[o + 5],
                fareYen = v[o + 6], balanceYen = v[o + 7], amountYen = v[o + 8],
                terminal = v[o + 9], process = v[o + 10],
                enter = if (v[o + 11] != 0) v[o + 12] to v[o + 13] else null,
                exit = if (v[o + 14] != 0) v[o + 15] to v[o + 16] else null,
                region = v[o + 17]
            )
        }
    }
}

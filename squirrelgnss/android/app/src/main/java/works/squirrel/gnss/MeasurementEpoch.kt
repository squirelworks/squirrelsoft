package works.squirrel.gnss

/**
 * One satellite observation as fed into [SqGnss.solveEpoch] (pseudorange
 * point positioning). Matches the packed JNI layout: parallel prn / range /
 * snr arrays.
 */
data class MeasObservation(
    val prn: Int,
    val pseudorangeM: Double,
    val snrDbHz: Double,
) {
    init {
        require(prn in 1..32) { "GPS PRN must be 1..32, got $prn" }
        require(pseudorangeM.isFinite() && pseudorangeM > 0.0) { "pseudorange must be positive finite" }
        require(snrDbHz.isFinite()) { "snr must be finite" }
    }
}

/**
 * A receiver epoch of packed GNSS observations ready for the JNI core.
 *
 * [tReceiver] is the epoch's receiver clock reading (GPS seconds of week).
 * [tEph] is the toe used for demo ephemerides in [SqGnss.solveEpoch] (for a
 * synthetic fill this is the original t_true; for phone rows it defaults to
 * [tReceiver] until real broadcast eph are wired).
 */
data class MeasurementEpoch(
    val tReceiver: Double,
    val observations: List<MeasObservation>,
    /** Where the measurements came from (synthetic CI path vs phone). */
    val source: Source = Source.SYNTHETIC,
    val tEph: Double = tReceiver,
) {
    enum class Source { SYNTHETIC, PHONE_GNSS, PHONE_LOCATION }

    /** @deprecated Prefer [tReceiver]. */
    val tGps: Double get() = tReceiver

    init {
        require(tReceiver.isFinite()) { "tReceiver must be finite" }
        require(tEph.isFinite()) { "tEph must be finite" }
        require(observations.size <= SqGnss.MAX_SATS) { "too many observations" }
    }

    data class Packed(val prns: IntArray, val ranges: DoubleArray, val snrs: DoubleArray, val n: Int)

    fun pack(cap: Int = SqGnss.MAX_SATS): Packed? {
        val n = observations.size
        if (n < 1 || n > cap) return null
        val prns = IntArray(cap)
        val ranges = DoubleArray(cap)
        val snrs = DoubleArray(cap)
        for (i in 0 until n) {
            val o = observations[i]
            prns[i] = o.prn
            ranges[i] = o.pseudorangeM
            snrs[i] = o.snrDbHz
        }
        return Packed(prns, ranges, snrs, n)
    }

    companion object {
        fun fromPacked(
            tReceiver: Double,
            prns: IntArray,
            ranges: DoubleArray,
            snrs: DoubleArray,
            n: Int,
            source: Source = Source.SYNTHETIC,
            tEph: Double = tReceiver,
        ): MeasurementEpoch? {
            if (n < 1 || n > prns.size || n > ranges.size || n > snrs.size) return null
            val obs = ArrayList<MeasObservation>(n)
            for (i in 0 until n) {
                val prn = prns[i]
                val range = ranges[i]
                val snr = snrs[i]
                if (prn !in 1..32 || !range.isFinite() || range <= 0.0 || !snr.isFinite()) return null
                obs.add(MeasObservation(prn, range, snr))
            }
            return MeasurementEpoch(tReceiver, obs, source, tEph)
        }
    }
}

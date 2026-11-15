package works.squirrel.gnss

/**
 * Pure mapping from Android-style raw GNSS clock + measurement fields to
 * [MeasObservation] rows. No Android framework types — unit tests and the
 * phone feed share the same math.
 *
 * Pseudorange from received SV time and the receiver clock (GPS week):
 *   pr = ((tRxWeekSec - tTxSec) mod week) * c
 * following the public GNSS measurement formulas (GPS ICD / Android docs).
 * Only GPS constellation rows are accepted here (PRN 1..32).
 */
object GnssRawMapper {
    const val C_LIGHT = 299792458.0
    const val GPS_WEEK_SEC = 604800.0
    const val CONSTELLATION_GPS = 1

    /**
     * Receiver time of week in seconds from Android [GnssClock]-shaped fields.
     * Returns null when bias is unknown (cannot form a pseudorange).
     */
    fun receiverTimeOfWeekSec(
        timeNanos: Long,
        fullBiasNanos: Long,
        biasNanos: Double,
        hasFullBias: Boolean,
        hasBias: Boolean,
    ): Double? {
        if (!hasFullBias) return null
        val bias = if (hasBias) biasNanos else 0.0
        val tRxSec = (timeNanos - fullBiasNanos).toDouble() * 1e-9 - bias * 1e-9
        if (!tRxSec.isFinite()) return null
        var tow = tRxSec % GPS_WEEK_SEC
        if (tow < 0) tow += GPS_WEEK_SEC
        return tow
    }

    /** GPS week length in nanoseconds (604800e9); fits in Long. */
    const val GPS_WEEK_NANOS = 604_800_000_000_000L

    /**
     * Pseudorange metres from receiver time-of-week and SV transmit time
     * (both GPS seconds of week). Diff is done in integer nanoseconds so
     * `Double * 1e-9` rounding does not break centimetre-class unit checks.
     */
    fun pseudorangeM(tRxWeekSec: Double, receivedSvTimeNanos: Long): Double? {
        if (!tRxWeekSec.isFinite()) return null
        var tRxNanos = kotlin.math.round(tRxWeekSec * 1e9).toLong() % GPS_WEEK_NANOS
        if (tRxNanos < 0) tRxNanos += GPS_WEEK_NANOS
        var tTxNanos = receivedSvTimeNanos % GPS_WEEK_NANOS
        if (tTxNanos < 0) tTxNanos += GPS_WEEK_NANOS
        var dtNanos = tRxNanos - tTxNanos
        if (dtNanos > GPS_WEEK_NANOS / 2) dtNanos -= GPS_WEEK_NANOS
        if (dtNanos < -GPS_WEEK_NANOS / 2) dtNanos += GPS_WEEK_NANOS
        val pr = dtNanos * 1e-9 * C_LIGHT
        if (!pr.isFinite() || pr <= 0.0 || pr > 1.0e8) return null
        return pr
    }

    /**
     * One GPS row → [MeasObservation], or null when the constellation /
     * state / range is unusable.
     */
    fun toObservation(
        constellationType: Int,
        svid: Int,
        cn0DbHz: Double,
        receivedSvTimeNanos: Long,
        tRxWeekSec: Double,
        stateOk: Boolean,
    ): MeasObservation? {
        if (!stateOk) return null
        if (constellationType != CONSTELLATION_GPS) return null
        if (svid !in 1..32) return null
        if (!cn0DbHz.isFinite()) return null
        val pr = pseudorangeM(tRxWeekSec, receivedSvTimeNanos) ?: return null
        return MeasObservation(prn = svid, pseudorangeM = pr, snrDbHz = cn0DbHz)
    }

    /**
     * Build an epoch from parallel measurement field arrays (synthetic or
     * mirrored from [android.location.GnssMeasurementsEvent]).
     */
    fun epochFromRaw(
        tReceiver: Double,
        timeNanos: Long,
        fullBiasNanos: Long,
        biasNanos: Double,
        hasFullBias: Boolean,
        hasBias: Boolean,
        constellation: IntArray,
        svid: IntArray,
        cn0: DoubleArray,
        receivedSvTimeNanos: LongArray,
        stateOk: BooleanArray,
        source: MeasurementEpoch.Source = MeasurementEpoch.Source.PHONE_GNSS,
        tEph: Double = tReceiver,
    ): MeasurementEpoch? {
        val n = constellation.size
        if (n == 0 || svid.size != n || cn0.size != n || receivedSvTimeNanos.size != n || stateOk.size != n) {
            return null
        }
        val tRx = receiverTimeOfWeekSec(timeNanos, fullBiasNanos, biasNanos, hasFullBias, hasBias) ?: return null
        val obs = ArrayList<MeasObservation>(n)
        for (i in 0 until n) {
            val o = toObservation(
                constellation[i], svid[i], cn0[i], receivedSvTimeNanos[i], tRx, stateOk[i],
            ) ?: continue
            obs.add(o)
        }
        if (obs.size < 4) return null
        return MeasurementEpoch(tReceiver = tReceiver, observations = obs, source = source, tEph = tEph)
    }
}

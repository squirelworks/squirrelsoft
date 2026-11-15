package works.squirrel.gnss

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Synthetic raw-measurement → pseudorange mapping (no Android framework).
 */
class GnssRawMapperTest {
    @Test
    fun receiverTimeOfWeekNeedsFullBias() {
        assertNull(
            GnssRawMapper.receiverTimeOfWeekSec(
                timeNanos = 1_000_000_000L,
                fullBiasNanos = -1_280_000_000_000_000_000L,
                biasNanos = 0.0,
                hasFullBias = false,
                hasBias = false,
            ),
        )
    }

    @Test
    fun pseudorangeFromSyntheticClockAndSvTime() {
        // 70 ms light-time → ~21e6 m (LEO-ish short; still exercises the math).
        val tRx = 100_000.070
        val tTxNanos = 100_000_000_000_000L // 100000.0 s in nanos
        val pr = GnssRawMapper.pseudorangeM(tRx, tTxNanos)
        assertNotNull(pr)
        assertEquals(0.070 * GnssRawMapper.C_LIGHT, pr!!, 1e-3)
    }

    @Test
    fun epochFromSyntheticRawGpsRows() {
        // Four GPS SVs with ~70–73 ms light times at a fixed receiver TOW.
        val tRxWeek = 100_000.070
        val fullBias = 0L
        val timeNanos = (tRxWeek * 1e9).toLong()
        val constellation = intArrayOf(1, 1, 1, 1, 3) // last is GLONASS — dropped
        val svid = intArrayOf(1, 2, 5, 12, 1)
        val cn0 = doubleArrayOf(42.0, 40.0, 38.0, 45.0, 30.0)
        val rxSv = longArrayOf(
            100_000_000_000_000L, // 100000.000 s
            99_999_999_000_000L, // 99999.999 s → 0.071 s dt
            99_999_998_000_000L,
            99_999_997_000_000L,
            100_000_000_000_000L,
        )
        val ok = booleanArrayOf(true, true, true, true, true)
        val epoch = GnssRawMapper.epochFromRaw(
            tReceiver = tRxWeek,
            timeNanos = timeNanos,
            fullBiasNanos = fullBias,
            biasNanos = 0.0,
            hasFullBias = true,
            hasBias = false,
            constellation = constellation,
            svid = svid,
            cn0 = cn0,
            receivedSvTimeNanos = rxSv,
            stateOk = ok,
            source = MeasurementEpoch.Source.SYNTHETIC,
        )
        assertNotNull(epoch)
        assertEquals(4, epoch!!.observations.size)
        assertEquals(MeasurementEpoch.Source.SYNTHETIC, epoch.source)
        assertTrue(epoch.observations.all { it.prn in 1..32 })
        assertTrue(epoch.observations.all { it.pseudorangeM in 1e6..1e8 })
    }

    @Test
    fun dropsUnlockedOrNonGps() {
        assertNull(
            GnssRawMapper.toObservation(
                constellationType = GnssRawMapper.CONSTELLATION_GPS,
                svid = 1,
                cn0DbHz = 40.0,
                receivedSvTimeNanos = 100_000_000_000_000L,
                tRxWeekSec = 100_000.07,
                stateOk = false,
            ),
        )
        assertNull(
            GnssRawMapper.toObservation(
                constellationType = 3,
                svid = 1,
                cn0DbHz = 40.0,
                receivedSvTimeNanos = 100_000_000_000_000L,
                tRxWeekSec = 100_000.07,
                stateOk = true,
            ),
        )
    }

    @Test
    fun tooFewGpsRowsYieldsNullEpoch() {
        val epoch = GnssRawMapper.epochFromRaw(
            tReceiver = 100000.0,
            timeNanos = 100_000_070_000_000L,
            fullBiasNanos = 0L,
            biasNanos = 0.0,
            hasFullBias = true,
            hasBias = false,
            constellation = intArrayOf(1, 1),
            svid = intArrayOf(1, 2),
            cn0 = doubleArrayOf(40.0, 40.0),
            receivedSvTimeNanos = longArrayOf(100_000_000_000_000L, 100_000_000_000_000L),
            stateOk = booleanArrayOf(true, true),
        )
        assertNull(epoch)
    }
}

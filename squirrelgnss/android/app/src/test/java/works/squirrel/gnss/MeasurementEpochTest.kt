package works.squirrel.gnss

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Synthetic measurement packing — no native library, no phone GNSS.
 */
class MeasurementEpochTest {
    @Test
    fun packAndRoundTrip() {
        val epoch = MeasurementEpoch(
            tReceiver = 100000.0,
            observations = listOf(
                MeasObservation(1, 22_000_000.0, 45.0),
                MeasObservation(2, 22_100_000.0, 40.0),
                MeasObservation(5, 21_900_000.0, 38.0),
                MeasObservation(12, 23_000_000.0, 42.0),
            ),
            source = MeasurementEpoch.Source.SYNTHETIC,
            tEph = 100000.0,
        )
        val packed = epoch.pack()
        assertNotNull(packed)
        assertEquals(4, packed!!.n)
        assertEquals(1, packed.prns[0])
        assertEquals(12, packed.prns[3])
        assertEquals(22_000_000.0, packed.ranges[0], 0.0)

        val back = MeasurementEpoch.fromPacked(
            epoch.tReceiver, packed.prns, packed.ranges, packed.snrs, packed.n, tEph = epoch.tEph,
        )
        assertNotNull(back)
        assertEquals(epoch.observations, back!!.observations)
        assertEquals(MeasurementEpoch.Source.SYNTHETIC, back.source)
        assertEquals(epoch.tEph, back.tEph, 0.0)
    }

    @Test
    fun fromPackedRejectsBadPrn() {
        val prns = intArrayOf(0, 2, 3, 4)
        val ranges = doubleArrayOf(2e7, 2e7, 2e7, 2e7)
        val snrs = doubleArrayOf(40.0, 40.0, 40.0, 40.0)
        assertNull(MeasurementEpoch.fromPacked(100000.0, prns, ranges, snrs, 4))
    }

    @Test
    fun fromPackedRejectsNonPositiveRange() {
        val prns = intArrayOf(1, 2, 3, 4)
        val ranges = doubleArrayOf(2e7, -1.0, 2e7, 2e7)
        val snrs = doubleArrayOf(40.0, 40.0, 40.0, 40.0)
        assertNull(MeasurementEpoch.fromPacked(100000.0, prns, ranges, snrs, 4))
    }

    @Test(expected = IllegalArgumentException::class)
    fun observationRejectsInvalidPrn() {
        MeasObservation(99, 2e7, 40.0)
    }

    @Test
    fun solveEpochWithoutNativeIsNull() {
        val epoch = MeasurementEpoch(
            tReceiver = 100000.0,
            observations = listOf(
                MeasObservation(1, 22e6, 40.0),
                MeasObservation(2, 22e6, 40.0),
                MeasObservation(3, 22e6, 40.0),
                MeasObservation(4, 22e6, 40.0),
            ),
        )
        assertTrue(!SqGnss.available)
        assertNull(SqGnss.solveEpoch(epoch))
    }
}

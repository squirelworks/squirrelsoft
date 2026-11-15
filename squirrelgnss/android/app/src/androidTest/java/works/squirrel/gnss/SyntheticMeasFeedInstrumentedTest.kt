package works.squirrel.gnss

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Instrumented checks for the synthetic measurement → JNI solve path.
 * Uses the packaged libsqgnss_jni.so (no phone GNSS hardware).
 * CI currently runs JVM unit tests only; this suite is for a device/emulator.
 */
@RunWith(AndroidJUnit4::class)
class SyntheticMeasFeedInstrumentedTest {
    @Test
    fun syntheticFeedSolvesToMillimetreClass() {
        assumeTrue("native sqgnss_jni required", SqGnss.available)
        val result = SyntheticMeasFeed.run(
            truth = SqGnss.Llh(35.681236, 139.767125, 40.0),
            tGps = 100000.0,
            clockBiasM = 1234.5,
            noiseM = 0.0,
            seed = 7,
        )
        assertNotNull(result)
        val fix = result!!.fix
        assertTrue("n_used=${fix.nUsed}", fix.nUsed >= 4)
        assertTrue("err=${fix.errM}", fix.errM >= 0.0 && fix.errM < 1e-3)
        assertTrue(kotlin.math.abs(fix.clockBiasM - 1234.5) < 1e-3)
        assertTrue(result.epoch.observations.size >= 4)
        assertTrue(result.epoch.source == MeasurementEpoch.Source.SYNTHETIC)
    }

    @Test
    fun fillThenSolveEpochRoundTrip() {
        assumeTrue("native sqgnss_jni required", SqGnss.available)
        val truth = SqGnss.Llh(35.681236, 139.767125, 40.0)
        val epoch = SqGnss.fillDemoEpoch(truth, tGps = 100000.0, clockBiasM = 15.0, noiseM = 0.0, seed = 1)
        assertNotNull(epoch)
        val fix = SqGnss.solveEpoch(epoch!!, truth)
        assertNotNull(fix)
        assertTrue(fix!!.errM < 1e-3)
    }

    @Test
    fun rawMapperSyntheticEpochPacksForSolve() {
        // Mapper-only path: build a MeasurementEpoch from synthetic field
        // arrays (same shape as GnssMeasurementsEvent), then require the
        // packed form to be accepted by solveEpoch when native is present.
        assumeTrue("native sqgnss_jni required", SqGnss.available)
        val filled = SqGnss.fillDemoEpoch(SyntheticMeasFeed.defaultTruth)!!
        val packed = filled.pack()!!
        val remapped = MeasurementEpoch.fromPacked(
            filled.tReceiver, packed.prns, packed.ranges, packed.snrs, packed.n,
            MeasurementEpoch.Source.SYNTHETIC,
            tEph = filled.tEph,
        )
        assertNotNull(remapped)
        val fix = SqGnss.solveEpoch(remapped!!, SyntheticMeasFeed.defaultTruth)
        assertNotNull(fix)
        assertTrue(fix!!.errM < 1e-3)
    }
}

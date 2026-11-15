package works.squirrel.gnss

import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test

class SqGnssTest {
    @Test
    fun withoutNativeCoreReturnsNull() {
        // JVM unit tests have no libsqgnss_jni.so.
        assertFalse(SqGnss.available)
        assertNull(SqGnss.llhToEcef(SqGnss.Llh(0.0, 0.0, 0.0)))
        assertNull(SqGnss.demoSolve(SqGnss.Llh(35.681236, 139.767125, 40.0)))
        assertNull(SqGnss.fillDemoEpoch(SqGnss.Llh(35.681236, 139.767125, 40.0)))
        assertNull(SyntheticMeasFeed.run())
        assertNull(SqGnss.ntripSynthMsm())
        assertNull(SqGnss.ntripLoopbackMsm())
        assertNull(NtripCasterFeed.runSynth())
        assertNull(SqGnss.ntripDial("127.0.0.1", "2101", "SYNTH"))
    }
}

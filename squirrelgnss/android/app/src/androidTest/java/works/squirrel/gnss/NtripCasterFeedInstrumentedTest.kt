package works.squirrel.gnss

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Instrumented checks for NTRIP → RTCM MSM through the packaged JNI library.
 * Memio synth and 127.0.0.1 loopback only — never dials a remote caster.
 * CI currently runs JVM unit tests; this suite is for a device/emulator.
 */
@RunWith(AndroidJUnit4::class)
class NtripCasterFeedInstrumentedTest {
    @Test
    fun synthMsm4ThroughMemio() {
        assumeTrue("native sqgnss_jni required", SqGnss.available)
        val result = NtripCasterFeed.runSynth()
        assertNotNull(result)
        assertTrue(result!!.isMsmStream)
        assertTrue("msg=${result.msgType}", result.msgType == 1074)
        assertTrue("msm=${result.msmType}", result.msmType == 4)
        assertTrue("stn=${result.stationId}", result.stationId == 2001)
        assertTrue("cells=${result.nCell}", result.nCell == 3)
        assertTrue("sats=${result.nSat}", result.nSat == 2)
        assertTrue(result.bodyLen > 0 && result.nFrames >= 1)
    }

    @Test
    fun loopbackTcpSyntheticCaster() {
        assumeTrue("native sqgnss_jni required", SqGnss.available)
        val result = NtripCasterFeed.runLoopback()
        assertNotNull(result)
        assertTrue(result!!.isMsmStream)
        assertTrue(result.msgType == 1074 && result.msmType == 4)
        assertTrue(result.nCell == 3)
    }
}

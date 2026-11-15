package works.squirrel.gnss

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM-only checks for NTRIP result helpers (no native library in unit tests).
 * Framing → MSM parse is covered by host `make test-jni` and instrumented tests.
 */
class NtripCasterFeedTest {
    @Test
    fun msmResultFlagsStream() {
        val ok = SqGnss.NtripMsmResult(
            status = 200,
            kind = 1,
            bodyLen = 64,
            nFrames = 1,
            msgType = 1074,
            stationId = 2001,
            epochMs = 345600000,
            msmType = 4,
            nSat = 2,
            nSig = 2,
            nCell = 3,
            category = 3,
        )
        assertTrue(ok.isMsmStream)
        assertEquals(1074, ok.msgType)
        assertEquals(4, ok.msmType)

        val bad = ok.copy(status = 401, kind = 3, nFrames = 0, msmType = 0)
        assertFalse(bad.isMsmStream)
    }
}

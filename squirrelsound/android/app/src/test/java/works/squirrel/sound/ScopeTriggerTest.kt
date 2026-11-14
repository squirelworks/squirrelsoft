package works.squirrel.sound

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

class ScopeTriggerTest {
    @Test
    fun risingEdgeOnSyntheticSineLocksNearZeroCrossing() {
        val n = 2048
        val sr = 48_000
        val hz = 1000.0
        // Phase offset so sample 0 is mid-cycle; the first rising zero is not at 0.
        val phase0 = 0.7
        val samples = FloatArray(n) { i ->
            sin(2.0 * PI * hz * i / sr + phase0).toFloat()
        }
        val span = ScopeTrigger.autoSpan(n)
        assertEquals(n / 2, span)
        val start = ScopeTrigger.risingEdgeStart(samples, span)
        assertTrue("expected a rising edge, got $start", start > 0)
        assertTrue(samples[start - 1] < 0f)
        assertTrue(samples[start] >= 0f)
        // Steady tone: the same edge distance repeats each period.
        val period = (sr / hz).toInt()
        val again = ScopeTrigger.risingEdgeStart(
            FloatArray(n) { i -> sin(2.0 * PI * hz * (i + period) / sr + phase0).toFloat() },
            span
        )
        assertEquals(start, again)
        assertTrue(ScopeTrigger.spanMs(span, sr) in 20f..25f)
    }

    @Test
    fun noCrossingFallsBackToStart() {
        val flat = FloatArray(128) { 0.5f }
        assertEquals(0, ScopeTrigger.risingEdgeStart(flat, 64))
        assertEquals(0, ScopeTrigger.risingEdgeStart(FloatArray(0), 10))
        assertEquals(64, ScopeTrigger.autoSpan(128))
        assertEquals(1, ScopeTrigger.autoSpan(1))
        assertEquals(0f, ScopeTrigger.spanMs(10, 0), 0f)
        assertTrue(abs(ScopeTrigger.spanMs(480, 48_000) - 10f) < 1e-3f)
    }
}

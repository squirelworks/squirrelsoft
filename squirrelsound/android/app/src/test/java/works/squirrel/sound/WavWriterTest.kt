package works.squirrel.sound

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class WavWriterTest {
    private fun le(b: ByteArray, at: Int) = ByteBuffer.wrap(b, at, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFFFFFFL

    @Test
    fun headerAndSamples() {
        val f = File.createTempFile("take", ".wav")
        try {
            val w = WavWriter(f, 48000)
            assertTrue(w.write(floatArrayOf(0f, 0.5f, -1f, 2f), 0, 4))
            assertTrue(w.write(floatArrayOf(9f, 0.25f), 1, 1))
            w.close()
            w.close() // twice is fine
            assertFalse(w.write(floatArrayOf(0f), 0, 1))
            val b = f.readBytes()
            assertEquals(44 + 10, b.size)
            assertEquals("RIFF", String(b, 0, 4))
            assertEquals(36L + 10, le(b, 4))
            assertEquals("WAVEfmt ", String(b, 8, 8))
            assertEquals(48000L, le(b, 24))
            assertEquals(96000L, le(b, 28))
            assertEquals("data", String(b, 36, 4))
            assertEquals(10L, le(b, 40))
            val pcm = ByteBuffer.wrap(b, 44, 10).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
            val s = ShortArray(5).also { pcm.get(it) }
            assertArrayEquals(shortArrayOf(0, 16383, -32767, 32767, 8191), s)
            assertEquals(5L, w.frames)
            assertEquals(5f / 48000, w.seconds, 1e-9f)
        } finally {
            f.delete()
        }
    }

    @Test
    fun stopsAtTheSizeLimit() {
        val f = File.createTempFile("full", ".wav")
        try {
            val w = WavWriter(f, 8000)
            w.maxFrames = 6
            assertTrue(w.write(FloatArray(4), 0, 4))
            assertFalse(w.write(FloatArray(4), 0, 4))
            assertTrue(w.full)
            assertEquals(6L, w.frames)
            w.close()
            assertEquals(44 + 12, f.length().toInt())
            assertEquals((0xFFFFFFFFL - 36) / 2, WavWriter.MAX_FRAMES)
            // the limit's header still fits 32 bits
            assertEquals(0xFFFFFFFFL - 1, 36 + WavWriter.MAX_FRAMES * 2)
        } finally {
            f.delete()
        }
    }

    @Test
    fun namesAndDurations() {
        assertTrue(Regex("squirrelsound-\\d{8}-\\d{6}\\.wav").matches(WavWriter.fileName(1_700_000_000_000L)))
        assertEquals("0:00", WavWriter.duration(0.4f))
        assertEquals("1:05", WavWriter.duration(65f))
        assertEquals("1:00:01", WavWriter.duration(3601f))
        assertEquals(44, WavWriter.header(44100, 0).size)
    }

    @Test
    fun calibrationRangeAndLabels() {
        assertEquals(120, Calibration.steps)
        assertEquals(60, Calibration.toProgress(0f))
        assertEquals(-60f, Calibration.fromProgress(-5), 0f)
        assertEquals(60f, Calibration.clamp(500f), 0f)
        assertEquals(0f, Calibration.clamp(Float.NaN), 0f)
        assertEquals(3f, Calibration.fromProgress(Calibration.toProgress(3f)), 0f)
        assertEquals("", Calibration.label(0f))
        assertEquals("cal +3 dB", Calibration.label(3f))
        assertEquals("cal -12 dB", Calibration.label(-12f))
        assertEquals("cal.unprocessed", Calibration.key("unprocessed"))
    }
}

package works.squirrel.sound

import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.RandomAccessFile
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Streams mono 16-bit PCM to a .wav file, the format of the desktop app's
 * recordings (squirrelsound/src/wavio.c). The header is written with zero
 * sizes and patched by [close]. RIFF sizes are 32 bits, so a take stops at
 * [maxFrames] (about 12 hours at 48 kHz) and [full] turns true.
 * Calls are synchronized: the capture thread writes, the UI thread closes.
 */
class WavWriter(val file: File, val sampleRate: Int) {
    private val out = BufferedOutputStream(FileOutputStream(file), 1 shl 16)
    private val bytes = ByteArray(8192)

    /** Frames written so far. */
    @Volatile var frames: Long = 0
        private set

    /** True once the 32-bit size limit is reached; nothing more is written. */
    @Volatile var full: Boolean = false
        private set

    @Volatile var closed: Boolean = false
        private set

    var maxFrames: Long = MAX_FRAMES
        internal set // tests shrink it

    init {
        require(sampleRate > 0)
        out.write(header(sampleRate, 0))
    }

    val seconds: Float get() = frames.toFloat() / sampleRate

    /** Appends [count] samples (-1..1, clipped). Returns false when closed or full. */
    @Synchronized
    fun write(samples: FloatArray, offset: Int, count: Int): Boolean {
        if (closed || full) return false
        var left = count.toLong().coerceAtMost(maxFrames - frames).toInt()
        if (left < count) full = true
        var i = offset
        while (left > 0) {
            val n = minOf(left, bytes.size / 2)
            for (k in 0 until n) {
                val v = (samples[i + k].coerceIn(-1f, 1f) * 32767f).toInt()
                bytes[2 * k] = v.toByte()
                bytes[2 * k + 1] = (v shr 8).toByte()
            }
            out.write(bytes, 0, 2 * n)
            frames += n
            i += n
            left -= n
        }
        return !full
    }

    /** Flushes, patches the sizes into the header and closes. Safe to call twice. */
    @Synchronized
    @Throws(IOException::class)
    fun close() {
        if (closed) return
        closed = true
        out.close()
        RandomAccessFile(file, "rw").use { raf ->
            val data = frames * 2
            raf.seek(4)
            raf.write(le32(36 + data))
            raf.seek(40)
            raf.write(le32(data))
        }
    }

    companion object {
        /** Largest data chunk whose RIFF size (36 + bytes) still fits 32 bits, in 16-bit frames. */
        const val MAX_FRAMES: Long = (0xFFFFFFFFL - 36) / 2

        private fun le32(v: Long) = byteArrayOf(v.toByte(), (v shr 8).toByte(), (v shr 16).toByte(), (v shr 24).toByte())
        private fun le16(v: Int) = byteArrayOf(v.toByte(), (v shr 8).toByte())

        /** The 44-byte header of a mono 16-bit PCM file with [dataBytes] of samples. */
        fun header(sampleRate: Int, dataBytes: Long): ByteArray =
            "RIFF".toByteArray() + le32(36 + dataBytes) + "WAVE".toByteArray() +
                "fmt ".toByteArray() + le32(16) + le16(1) + le16(1) + le32(sampleRate.toLong()) +
                le32(sampleRate * 2L) + le16(2) + le16(16) +
                "data".toByteArray() + le32(dataBytes)

        /** squirrelsound-YYYYMMDD-HHMMSS.wav in local time. */
        fun fileName(nowMs: Long): String =
            "squirrelsound-" + SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date(nowMs)) + ".wav"

        /** m:ss (h:mm:ss past an hour) for a take's length. */
        fun duration(seconds: Float): String {
            val t = seconds.toLong().coerceAtLeast(0)
            return if (t >= 3600) "%d:%02d:%02d".format(Locale.ROOT, t / 3600, t / 60 % 60, t % 60)
            else "%d:%02d".format(Locale.ROOT, t / 60, t % 60)
        }
    }
}

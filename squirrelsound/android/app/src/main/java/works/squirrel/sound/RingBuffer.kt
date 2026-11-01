package works.squirrel.sound

/**
 * Mono PCM float ring used to decouple AudioRecord reads from FFT hops.
 * Clean-room helper (not derived from Spectroid).
 */
class RingBuffer(capacity: Int) {
    init {
        require(capacity >= 2) { "capacity" }
    }

    private val buf = FloatArray(capacity)
    private var write = 0
    private var size = 0

    val capacity: Int get() = buf.size
    val available: Int get() = size

    fun clear() {
        write = 0
        size = 0
    }

    fun write(samples: FloatArray, offset: Int, count: Int) {
        require(offset >= 0 && count >= 0 && offset + count <= samples.size)
        if (count == 0) return
        val cap = buf.size
        var off = offset
        var left = count
        while (left > 0) {
            val chunk = minOf(left, cap - write)
            System.arraycopy(samples, off, buf, write, chunk)
            write = (write + chunk) % cap
            off += chunk
            left -= chunk
        }
        size = minOf(cap, size + count)
    }

    /**
     * Copy the most recent [fftSize] samples into [out] (oldest of the window first).
     * Keeps [fftSize - hop] samples of overlap and drops any backlog so the live
     * display stays near real time. Returns false if fewer than [fftSize] samples
     * are buffered.
     */
    fun takeWindow(out: FloatArray, fftSize: Int, hop: Int): Boolean {
        require(out.size >= fftSize)
        require(hop in 1..fftSize)
        if (size < fftSize) return false
        val cap = buf.size
        val start = (write - fftSize + cap) % cap
        var idx = start
        for (i in 0 until fftSize) {
            out[i] = buf[idx]
            idx = (idx + 1) % cap
        }
        size = fftSize - hop
        return true
    }
}

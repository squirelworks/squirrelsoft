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
     * Copy the oldest unconsumed [fftSize] samples into [out] (oldest first), then
     * consume exactly [hop] samples, so frames per second = sampleRate / hop no
     * matter how the reads are sized. If more than [maxBacklog] samples beyond one
     * window are waiting (the UI fell behind), the oldest are dropped first to stay
     * live. Returns false if fewer than [fftSize] samples are buffered.
     */
    fun takeWindow(out: FloatArray, fftSize: Int, hop: Int, maxBacklog: Int = 4 * fftSize): Boolean {
        require(out.size >= fftSize)
        require(hop in 1..fftSize)
        require(maxBacklog >= 0)
        if (size < fftSize) return false
        if (size > fftSize + maxBacklog) size = fftSize + maxBacklog
        val cap = buf.size
        var idx = (write - size + cap) % cap
        for (i in 0 until fftSize) {
            out[i] = buf[idx]
            idx = (idx + 1) % cap
        }
        size -= hop
        return true
    }
}

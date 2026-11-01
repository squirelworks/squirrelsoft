package works.squirrel.sound

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Handler
import android.os.Looper
import kotlin.math.PI
import kotlin.math.sin

/**
 * Mic (AudioRecord) or demo tone → windowed FFT magnitudes (dB).
 * Mirrors desktop SquirrelSound live mode on Android.
 */
class AudioEngine(
    private val onFrame: (db: FloatArray, sampleRate: Int, peakHz: Float) -> Unit
) {
    enum class Source { MIC, DEMO }

    @Volatile private var running = false
    private var thread: Thread? = null
    private val main = Handler(Looper.getMainLooper())

    var fftSize: Int = 2048
    var source: Source = Source.MIC
    var demoHz: Float = 440f

    fun start() {
        stop()
        running = true
        val size = fftSize
        thread = Thread({ loop(size) }, "sq-sound-fft").also { it.start() }
    }

    fun stop() {
        running = false
        thread?.join(500)
        thread = null
    }

    private fun loop(size: Int) {
        val re = FloatArray(size)
        val im = FloatArray(size)
        val window = Fft.hann(size)
        val mags = FloatArray(size / 2)
        val hold = FloatArray(size / 2) { -90f }
        var phase = 0.0
        var recorder: AudioRecord? = null
        var sampleRate = 44100
        val shortBuf = ShortArray(size)

        try {
            if (source == Source.MIC) {
                sampleRate = 44100
                val min = AudioRecord.getMinBufferSize(
                    sampleRate,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT
                )
                recorder = AudioRecord(
                    MediaRecorder.AudioSource.VOICE_RECOGNITION,
                    sampleRate,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                    maxOf(min, size * 4)
                )
                if (recorder.state != AudioRecord.STATE_INITIALIZED) {
                    recorder.release()
                    recorder = null
                    source = Source.DEMO
                } else {
                    recorder.startRecording()
                }
            }

            while (running) {
                if (source == Source.MIC && recorder != null) {
                    var got = 0
                    while (got < size && running) {
                        val n = recorder.read(shortBuf, got, size - got)
                        if (n <= 0) break
                        got += n
                    }
                    for (i in 0 until size) {
                        re[i] = (shortBuf[i] / 32768f) * window[i]
                        im[i] = 0f
                    }
                } else {
                    val sr = sampleRate.toDouble()
                    val f = demoHz.toDouble()
                    for (i in 0 until size) {
                        val s = (0.25 * sin(phase)).toFloat()
                        phase += 2.0 * PI * f / sr
                        if (phase > 2.0 * PI) phase -= 2.0 * PI
                        re[i] = s * window[i]
                        im[i] = 0f
                    }
                    try {
                        Thread.sleep((1000L * size / sampleRate).coerceAtLeast(8L))
                    } catch (_: InterruptedException) {
                        break
                    }
                }

                Fft.forward(re, im)
                Fft.magnitudeDb(re, im, mags)

                var peakBin = 0
                var peakDb = -999f
                for (i in 1 until mags.size) {
                    if (mags[i] > peakDb) {
                        peakDb = mags[i]
                        peakBin = i
                    }
                    if (mags[i] > hold[i]) hold[i] = mags[i]
                }
                val peakHz = peakBin * sampleRate.toFloat() / size
                val copy = mags.copyOf()
                main.post {
                    if (running) onFrame(copy, sampleRate, peakHz)
                }
            }
        } finally {
            try {
                recorder?.stop()
            } catch (_: Exception) {
            }
            recorder?.release()
        }
    }
}

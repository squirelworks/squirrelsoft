package works.squirrel.sound

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Build
import android.os.Handler
import android.os.Looper
import kotlin.math.PI
import kotlin.math.sin

/**
 * Mic (AudioRecord) or demo tone → windowed FFT magnitudes (dB).
 * Tries UNPROCESSED / MIC first so the spectrum looks like Spectroid,
 * not a voice-recognition AGC path.
 */
class AudioEngine(
    private val onFrame: (db: FloatArray, sampleRate: Int, peakHz: Float) -> Unit,
    private val onInfo: (msg: String) -> Unit = {}
) {
    enum class Source { MIC, DEMO }

    @Volatile private var running = false
    private var thread: Thread? = null
    private val main = Handler(Looper.getMainLooper())

    var fftSize: Int = 2048
    var source: Source = Source.MIC
    var demoHz: Float = 440f

    /** Actual capture path after open (may differ from requested [source]). */
    @Volatile var activeLabel: String = "idle"
        private set

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
        activeLabel = "idle"
    }

    private fun openMic(sampleRate: Int, bufBytes: Int): Pair<AudioRecord, String>? {
        val candidates = mutableListOf<Pair<Int, String>>()
        if (Build.VERSION.SDK_INT >= 24) {
            candidates += MediaRecorder.AudioSource.UNPROCESSED to "mic-unprocessed"
        }
        candidates += MediaRecorder.AudioSource.MIC to "mic"
        candidates += MediaRecorder.AudioSource.CAMCORDER to "mic-camcorder"
        candidates += MediaRecorder.AudioSource.VOICE_RECOGNITION to "mic-voice"
        candidates += MediaRecorder.AudioSource.DEFAULT to "mic-default"

        for ((src, label) in candidates) {
            var rec: AudioRecord? = null
            try {
                rec = AudioRecord(
                    src,
                    sampleRate,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,
                    bufBytes
                )
                if (rec.state == AudioRecord.STATE_INITIALIZED) {
                    return rec to label
                }
                rec.release()
            } catch (_: Exception) {
                try {
                    rec?.release()
                } catch (_: Exception) {
                }
            }
        }
        return null
    }

    private fun loop(size: Int) {
        val re = FloatArray(size)
        val im = FloatArray(size)
        val window = Fft.hann(size)
        val mags = FloatArray(size / 2)
        var phase = 0.0
        var recorder: AudioRecord? = null
        var sampleRate = 44100
        val shortBuf = ShortArray(size)
        var useMic = source == Source.MIC

        try {
            if (useMic) {
                for (sr in intArrayOf(44100, 48000, 16000)) {
                    val min = AudioRecord.getMinBufferSize(
                        sr,
                        AudioFormat.CHANNEL_IN_MONO,
                        AudioFormat.ENCODING_PCM_16BIT
                    )
                    if (min <= 0) continue
                    val opened = openMic(sr, maxOf(min, size * 4))
                    if (opened != null) {
                        recorder = opened.first
                        activeLabel = opened.second
                        sampleRate = sr
                        break
                    }
                }
                if (recorder == null) {
                    useMic = false
                    activeLabel = "demo (mic open failed)"
                    main.post { onInfo("Microphone unavailable — falling back to demo 440 Hz") }
                } else {
                    try {
                        recorder.startRecording()
                        main.post { onInfo("Capturing via $activeLabel @ ${sampleRate} Hz") }
                    } catch (e: Exception) {
                        recorder.release()
                        recorder = null
                        useMic = false
                        activeLabel = "demo (startRecording failed)"
                        main.post { onInfo("Mic start failed: ${e.message}") }
                    }
                }
            } else {
                activeLabel = "demo 440 Hz"
                main.post { onInfo("Demo tone 440 Hz") }
            }

            while (running) {
                if (useMic && recorder != null) {
                    var got = 0
                    while (got < size && running) {
                        val n = recorder.read(shortBuf, got, size - got)
                        if (n == AudioRecord.ERROR_INVALID_OPERATION ||
                            n == AudioRecord.ERROR_BAD_VALUE ||
                            n == AudioRecord.ERROR_DEAD_OBJECT
                        ) {
                            useMic = false
                            activeLabel = "demo (read error)"
                            main.post { onInfo("Mic read failed — switching to demo tone") }
                            break
                        }
                        if (n <= 0) break
                        got += n
                    }
                    if (!useMic) continue
                    // Zero-pad a short read so FFT stays defined; next frame catches up.
                    for (i in got until size) shortBuf[i] = 0
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
            try {
                recorder?.release()
            } catch (_: Exception) {
            }
        }
    }
}

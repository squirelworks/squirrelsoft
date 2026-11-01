package works.squirrel.sound

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Build
import android.os.Handler
import android.os.Looper
import kotlin.math.PI
import kotlin.math.log10
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Continuous mic (or demo tone) → hopped windowed FFT magnitudes (dBFS).
 *
 * Architecture inspired by Spectroid UX docs (Carl Reinke): continuous capture,
 * fixed transform interval / hop, dBFS scaling, live spectrum + waterfall feed.
 * Implementation is original — no Spectroid source was used.
 *
 * Prefers [MediaRecorder.AudioSource.UNPROCESSED] then MIC so the graph is not
 * stuck on a voice-recognition AGC path.
 */
class AudioEngine(
    private val onFrame: (SpectrumFrame) -> Unit,
    private val onStatus: (msg: String) -> Unit = {}
) {
    enum class Source { MIC, DEMO }

    @Volatile private var running = false
    private var thread: Thread? = null
    private val main = Handler(Looper.getMainLooper())

    var fftSize: Int = 2048
    /** Target analysis hop in milliseconds (Spectroid default interval ≈ 20 ms). */
    var hopMs: Int = 20
    var source: Source = Source.MIC
    var demoHz: Float = 440f
    /** Exponential smoothing 0..1 (0 = off). Spectroid-like default ≈ 0.3. */
    var smooth: Float = 0.30f

    @Volatile var activeLabel: String = "idle"
        private set

    @Volatile var lastError: String? = null
        private set

    fun start() {
        stop()
        lastError = null
        running = true
        val size = fftSize
        val hop = hopMs
        thread = Thread({ loop(size, hop) }, "sq-sound-fft").also {
            it.priority = Thread.NORM_PRIORITY + 1
            it.start()
        }
    }

    fun stop() {
        running = false
        val t = thread
        thread = null
        if (t != null) {
            t.join(750)
            if (t.isAlive) {
                t.interrupt()
                t.join(250)
            }
        }
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
            } catch (e: SecurityException) {
                try {
                    rec?.release()
                } catch (_: Exception) {
                }
                lastError = "RECORD_AUDIO denied"
                return null
            } catch (_: Exception) {
                try {
                    rec?.release()
                } catch (_: Exception) {
                }
            }
        }
        return null
    }

    private fun postStatus(msg: String) {
        main.post { onStatus(msg) }
    }

    private fun loop(size: Int, hopMsCfg: Int) {
        val re = FloatArray(size)
        val im = FloatArray(size)
        val window = Fft.hann(size)
        val mags = FloatArray(size / 2)
        val smoothed = FloatArray(size / 2) { -90f }
        var smoothInit = false
        var phase = 0.0
        var recorder: AudioRecord? = null
        var sampleRate = 44100
        val shortBuf = ShortArray(size)
        val floatChunk = FloatArray(size)
        val ring = RingBuffer(size * 8)
        val wantMic = source == Source.MIC
        var useMic = wantMic

        try {
            if (useMic) {
                for (sr in intArrayOf(48000, 44100, 16000)) {
                    val min = AudioRecord.getMinBufferSize(
                        sr,
                        AudioFormat.CHANNEL_IN_MONO,
                        AudioFormat.ENCODING_PCM_16BIT
                    )
                    if (min <= 0) continue
                    val opened = openMic(sr, maxOf(min * 2, size * 8))
                    if (opened != null) {
                        recorder = opened.first
                        activeLabel = opened.second
                        sampleRate = sr
                        break
                    }
                }
                if (recorder == null) {
                    useMic = false
                    activeLabel = "mic-failed"
                    lastError = lastError ?: "Microphone unavailable"
                    postStatus(lastError!!)
                    if (wantMic) {
                        // Do not silently substitute a demo tone when the user asked for mic.
                        return
                    }
                } else {
                    try {
                        recorder.startRecording()
                        if (recorder.recordingState != AudioRecord.RECORDSTATE_RECORDING) {
                            throw IllegalStateException("recordingState=${recorder.recordingState}")
                        }
                        postStatus("Capturing via $activeLabel @ ${sampleRate} Hz")
                    } catch (e: Exception) {
                        try {
                            recorder.release()
                        } catch (_: Exception) {
                        }
                        recorder = null
                        useMic = false
                        activeLabel = "mic-failed"
                        lastError = "Mic start failed: ${e.message}"
                        postStatus(lastError!!)
                        if (wantMic) return
                    }
                }
            } else {
                activeLabel = "demo 440 Hz"
                postStatus("Demo tone 440 Hz")
            }

            val hop = ((sampleRate * hopMsCfg) / 1000).coerceIn(size / 16, size)
            val demoScratch = FloatArray(hop.coerceAtLeast(64))

            while (running) {
                if (useMic && recorder != null) {
                    val n = if (Build.VERSION.SDK_INT >= 23) {
                        recorder.read(shortBuf, 0, shortBuf.size, AudioRecord.READ_BLOCKING)
                    } else {
                        recorder.read(shortBuf, 0, shortBuf.size)
                    }
                    when {
                        n == AudioRecord.ERROR_INVALID_OPERATION ||
                            n == AudioRecord.ERROR_BAD_VALUE ||
                            n == AudioRecord.ERROR_DEAD_OBJECT ||
                            n == AudioRecord.ERROR -> {
                            activeLabel = "mic-failed"
                            lastError = "Mic read error ($n)"
                            postStatus(lastError!!)
                            break
                        }
                        n <= 0 -> {
                            // Spurious empty read — brief yield, do not FFT zeros.
                            try {
                                Thread.sleep(2)
                            } catch (_: InterruptedException) {
                                break
                            }
                            continue
                        }
                        else -> {
                            for (i in 0 until n) {
                                floatChunk[i] = shortBuf[i] / 32768f
                            }
                            ring.write(floatChunk, 0, n)
                        }
                    }
                } else {
                    // Demo: push ~hop samples of a sine at wall-clock pace.
                    val sr = sampleRate.toDouble()
                    val f = demoHz.toDouble()
                    val count = demoScratch.size
                    for (i in 0 until count) {
                        demoScratch[i] = (0.25 * sin(phase)).toFloat()
                        phase += 2.0 * PI * f / sr
                        if (phase > 2.0 * PI) phase -= 2.0 * PI
                    }
                    ring.write(demoScratch, 0, count)
                    try {
                        Thread.sleep((1000L * count / sampleRate).coerceAtLeast(4L))
                    } catch (_: InterruptedException) {
                        break
                    }
                }

                // Emit as many hopped frames as the ring allows (usually 0–1 per read).
                while (running && ring.available >= size) {
                    if (!ring.takeWindow(re, size, hop)) break
                    var sumSq = 0.0
                    for (i in 0 until size) {
                        val s = re[i]
                        sumSq += (s * s).toDouble()
                        re[i] = s * window[i]
                        im[i] = 0f
                    }
                    val rms = sqrt(sumSq / size).toFloat()
                    val rmsDb = if (rms > 1e-12f) 20f * log10(rms) else -90f

                    Fft.forward(re, im)
                    Fft.magnitudeDb(re, im, mags)

                    val a = smooth.coerceIn(0f, 0.95f)
                    if (!smoothInit || a <= 0f) {
                        for (i in mags.indices) smoothed[i] = mags[i]
                        smoothInit = true
                    } else {
                        val keep = 1f - a
                        for (i in mags.indices) {
                            smoothed[i] = smoothed[i] * keep + mags[i] * a
                        }
                    }

                    var peakBin = 0
                    var peakDb = -999f
                    for (i in 1 until smoothed.size) {
                        if (smoothed[i] > peakDb) {
                            peakDb = smoothed[i]
                            peakBin = i
                        }
                    }
                    val peakHz = peakBin * sampleRate.toFloat() / size
                    val frame = SpectrumFrame(
                        db = smoothed.copyOf(),
                        sampleRate = sampleRate,
                        fftSize = size,
                        peakHz = peakHz,
                        peakDb = peakDb,
                        rmsDb = rmsDb,
                        sourceLabel = activeLabel
                    )
                    main.post {
                        if (running) onFrame(frame)
                    }
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

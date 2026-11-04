package works.squirrel.sound

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
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
 * stuck on a voice-recognition AGC path. Transient AudioRecord failures restart
 * capture instead of permanently stopping the UI feed.
 *
 * [start]/[stop] are session-scoped: an old capture thread that outlives
 * [stop]'s join cannot keep posting frames after a restart, and [stop] releases
 * the active [AudioRecord] so blocking reads unblock promptly (avoids ANR /
 * double-start races on the UI thread).
 */
class AudioEngine(
    private val appContext: Context,
    private val onFrame: (SpectrumFrame) -> Unit,
    private val onStatus: (msg: String) -> Unit = {}
) {
    enum class Source { MIC, DEMO }

    @Volatile private var running = false
    private var thread: Thread? = null
    private val main = Handler(Looper.getMainLooper())
    private val sessionGen = AtomicInteger(0)
    private val activeRecorder = AtomicReference<AudioRecord?>(null)
    private val startStopLock = Any()

    var fftSize: Int = 2048
    /**
     * Time the visible LOFARgram covers, in seconds. Re-read live so the Basic
     * "Span" setting changes the scroll rate without restarting capture.
     */
    @Volatile var waterfallSeconds: Float = SpectrumControls.WaterfallDuration.DEFAULT.seconds
    /** Rows in the LOFARgram bitmap the frames are planned for. */
    @Volatile var waterfallRows: Int = SpectrumControls.WATERFALL_ROWS
    var source: Source = Source.MIC
    var demoHz: Float = 440f
    /** Exponential smoothing 0..1 (0 = off). Spectroid-like default ≈ 0.3. */
    @Volatile var smooth: Float = 0.30f

    @Volatile var activeLabel: String = "idle"
        private set

    @Volatile var lastError: String? = null
        private set

    private val audioManager =
        appContext.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private var focusRequest: AudioFocusRequest? = null
    private var wakeLock: PowerManager.WakeLock? = null

    private val focusChangeListener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_LOSS,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                // Keep reading; OEM may reclaim briefly. Restart path recovers.
                postStatus("Audio focus lost ($change) — keeping capture")
            }
            AudioManager.AUDIOFOCUS_GAIN -> {
                postStatus("Audio focus regained")
            }
        }
    }

    fun start() {
        synchronized(startStopLock) {
            stopLocked()
            lastError = null
            val session = sessionGen.incrementAndGet()
            running = true
            acquireKeepAlive()
            val size = fftSize.coerceIn(256, 16384).takeHighestOneBit().coerceAtLeast(256)
            fftSize = size
            // hopMs is read live inside the capture loop (Scroll speed).
            hopMs = hopMs.coerceIn(5, 200)
            thread = Thread({ loop(size, session) }, "sq-sound-fft").also {
                it.priority = Thread.NORM_PRIORITY + 1
                it.isDaemon = true
                it.uncaughtExceptionHandler = Thread.UncaughtExceptionHandler { _, e ->
                    lastError = "Capture crashed: ${e.javaClass.simpleName}: ${e.message}"
                    postStatus(lastError!!)
                    activeLabel = "error"
                    running = false
                    releaseKeepAlive()
                    activeRecorder.getAndSet(null)?.let { releaseRecorder(it) }
                }
                it.start()
            }
        }
    }

    fun stop() {
        synchronized(startStopLock) {
            stopLocked()
        }
    }

    private fun stopLocked() {
        running = false
        sessionGen.incrementAndGet() // invalidate any lingering loop
        // Unblock AudioRecord.READ_BLOCKING so join does not stall the UI thread.
        activeRecorder.getAndSet(null)?.let { releaseRecorder(it) }
        val t = thread
        thread = null
        if (t != null) {
            t.interrupt()
            try {
                t.join(400)
            } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
            }
            if (t.isAlive) {
                // Last resort: wait a bit more; session id already invalidates work.
                try {
                    t.join(200)
                } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                }
            }
        }
        releaseKeepAlive()
        activeLabel = "idle"
    }

    private fun acquireKeepAlive() {
        requestAudioFocus()
        try {
            val pm = appContext.getSystemService(Context.POWER_SERVICE) as PowerManager
            val wl = pm.newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK,
                "SquirrelSound:capture"
            )
            wl.setReferenceCounted(false)
            wl.acquire(60 * 60 * 1000L) // capped; stop() always releases
            wakeLock = wl
        } catch (_: Exception) {
            wakeLock = null
        }
    }

    private fun releaseKeepAlive() {
        abandonAudioFocus()
        try {
            wakeLock?.let { if (it.isHeld) it.release() }
        } catch (_: Exception) {
        }
        wakeLock = null
    }

    private fun requestAudioFocus() {
        try {
            if (Build.VERSION.SDK_INT >= 26) {
                val attrs = AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
                val req = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(attrs)
                    .setOnAudioFocusChangeListener(focusChangeListener, main)
                    .setAcceptsDelayedFocusGain(true)
                    .build()
                focusRequest = req
                audioManager.requestAudioFocus(req)
            } else {
                @Suppress("DEPRECATION")
                audioManager.requestAudioFocus(
                    focusChangeListener,
                    AudioManager.STREAM_MUSIC,
                    AudioManager.AUDIOFOCUS_GAIN
                )
            }
        } catch (_: Exception) {
            focusRequest = null
        }
    }

    private fun abandonAudioFocus() {
        try {
            if (Build.VERSION.SDK_INT >= 26) {
                focusRequest?.let { audioManager.abandonAudioFocusRequest(it) }
            } else {
                @Suppress("DEPRECATION")
                audioManager.abandonAudioFocus(focusChangeListener)
            }
        } catch (_: Exception) {
        }
        focusRequest = null
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
            } catch (_: IllegalArgumentException) {
                try {
                    rec?.release()
                } catch (_: Exception) {
                }
            } catch (_: Exception) {
                try {
                    rec?.release()
                } catch (_: Exception) {
                }
            }
        }
        return null
    }

    private fun releaseRecorder(rec: AudioRecord?) {
        if (rec == null) return
        try {
            if (rec.recordingState == AudioRecord.RECORDSTATE_RECORDING) {
                rec.stop()
            }
        } catch (_: IllegalStateException) {
        } catch (_: Exception) {
        }
        try {
            rec.release()
        } catch (_: Exception) {
        }
    }

    /** Open + startRecording; returns null if all candidates fail. */
    private fun startMicSession(): Triple<AudioRecord, String, Int>? {
        for (sr in intArrayOf(48000, 44100, 16000)) {
            val min = try {
                AudioRecord.getMinBufferSize(
                    sr,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT
                )
            } catch (_: Exception) {
                continue
            }
            if (min <= 0) continue
            val opened = openMic(sr, maxOf(min * 2, fftSize * 8))
            if (opened != null) {
                val (rec, label) = opened
                try {
                    rec.startRecording()
                    if (rec.recordingState != AudioRecord.RECORDSTATE_RECORDING) {
                        throw IllegalStateException("recordingState=${rec.recordingState}")
                    }
                    return Triple(rec, label, sr)
                } catch (e: IllegalStateException) {
                    releaseRecorder(rec)
                    lastError = "Mic start failed: ${e.message}"
                } catch (e: SecurityException) {
                    releaseRecorder(rec)
                    lastError = "RECORD_AUDIO denied"
                    return null
                } catch (e: Exception) {
                    releaseRecorder(rec)
                    lastError = "Mic start failed: ${e.message}"
                }
            } else if (lastError == "RECORD_AUDIO denied") {
                return null
            }
        }
        if (lastError == null) {
            lastError = "Microphone unavailable (busy or unsupported)"
        }
        return null
    }

    private fun postStatus(msg: String) {
        main.post {
            try {
                onStatus(msg)
            } catch (_: Exception) {
                // Activity may be gone; never crash the process from a status post.
            }
        }
    }

    private fun postFrame(session: Int, frame: SpectrumFrame) {
        main.post {
            if (!running || sessionGen.get() != session) return@post
            try {
                onFrame(frame)
            } catch (_: Exception) {
                // Swallow UI-side failures (destroyed view hierarchy, etc.).
            }
        }
    }

    private fun stillActive(session: Int): Boolean =
        running && sessionGen.get() == session && !Thread.currentThread().isInterrupted

    private fun loop(size: Int, session: Int) {
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
        val rows = WaterfallRowAccumulator()
        val wantMic = source == Source.MIC
        var useMic = wantMic
        var consecutiveRestarts = 0
        val maxRestartsBeforeBackoff = 8

        fun adoptRecorder(rec: AudioRecord, label: String, sr: Int) {
            recorder = rec
            activeRecorder.set(rec)
            activeLabel = label
            sampleRate = sr
        }

        fun dropRecorder() {
            val r = recorder
            recorder = null
            activeRecorder.compareAndSet(r, null)
            releaseRecorder(r)
        }

        try {
            if (useMic) {
                val sessionOpen = startMicSession()
                if (sessionOpen == null) {
                    useMic = false
                    activeLabel = "mic-failed"
                    lastError = lastError ?: "Microphone unavailable"
                    postStatus(lastError!!)
                    // Keep the capture thread alive and retry; do not fall back to demo.
                } else {
                    adoptRecorder(sessionOpen.first, sessionOpen.second, sessionOpen.third)
                    consecutiveRestarts = 0
                    postStatus("Capturing via $activeLabel @ ${sampleRate} Hz")
                }
            } else {
                activeLabel = "demo 440 Hz"
                postStatus("Demo tone 440 Hz")
            }

            if (!stillActive(session)) return

            val demoScratch = FloatArray(size.coerceAtLeast(64))

            while (stillActive(session)) {
                val plan = SpectrumControls.rowPlan(waterfallSeconds, waterfallRows, sampleRate, size)
                val hop = plan.hopSamples
                if (useMic && recorder != null) {
                    val n = try {
                        if (Build.VERSION.SDK_INT >= 23) {
                            recorder!!.read(shortBuf, 0, hop, AudioRecord.READ_BLOCKING)
                        } else {
                            recorder!!.read(shortBuf, 0, hop)
                        }
                    } catch (e: IllegalStateException) {
                        lastError = "Mic read IllegalState — restarting"
                        postStatus(lastError!!)
                        dropRecorder()
                        consecutiveRestarts++
                        sleepQuiet((50L * consecutiveRestarts).coerceAtMost(1500L))
                        if (!stillActive(session)) break
                        val opened = startMicSession()
                        if (opened == null) {
                            activeLabel = "mic-failed"
                            lastError = lastError ?: "Microphone unavailable after restart"
                            postStatus(lastError!!)
                            useMic = false
                            continue
                        }
                        adoptRecorder(opened.first, opened.second, opened.third)
                        ring.clear()
                        rows.reset()
                        continue
                    } catch (e: SecurityException) {
                        lastError = "RECORD_AUDIO denied"
                        activeLabel = "mic-denied"
                        postStatus(lastError!!)
                        dropRecorder()
                        useMic = false
                        // Stay alive so UI can show the error; user must Stop / re-grant.
                        sleepQuiet(1000)
                        continue
                    } catch (e: Exception) {
                        lastError = "Mic read error: ${e.message}"
                        postStatus(lastError!!)
                        dropRecorder()
                        consecutiveRestarts++
                        sleepQuiet(200)
                        continue
                    }
                    when {
                        n == AudioRecord.ERROR_INVALID_OPERATION ||
                            n == AudioRecord.ERROR_BAD_VALUE ||
                            n == AudioRecord.ERROR_DEAD_OBJECT ||
                            n == AudioRecord.ERROR -> {
                            lastError = "Mic read error ($n) — restarting"
                            postStatus(lastError!!)
                            dropRecorder()
                            consecutiveRestarts++
                            sleepQuiet((50L * consecutiveRestarts).coerceAtMost(1500L))
                            if (!stillActive(session)) break
                            val opened = startMicSession()
                            if (opened == null) {
                                activeLabel = "mic-failed"
                                lastError = lastError ?: "Microphone unavailable after restart"
                                postStatus(lastError!!)
                                if (consecutiveRestarts >= maxRestartsBeforeBackoff) {
                                    sleepQuiet(2000)
                                    consecutiveRestarts = maxRestartsBeforeBackoff / 2
                                }
                                useMic = false
                                continue
                            }
                            adoptRecorder(opened.first, opened.second, opened.third)
                            ring.clear()
                            rows.reset()
                            if (consecutiveRestarts <= 2) {
                                postStatus("Mic restarted via $activeLabel @ ${sampleRate} Hz")
                            }
                            continue
                        }
                        n <= 0 -> {
                            // Spurious empty / short read — brief yield, do not FFT zeros.
                            sleepQuiet(2)
                            continue
                        }
                        else -> {
                            consecutiveRestarts = 0
                            val count = n.coerceAtMost(shortBuf.size)
                            for (i in 0 until count) {
                                floatChunk[i] = shortBuf[i] / 32768f
                            }
                            ring.write(floatChunk, 0, count)
                        }
                    }
                } else if (wantMic && !useMic) {
                    // Waiting to recover a failed open, or after permission denial.
                    if (lastError == "RECORD_AUDIO denied") {
                        sleepQuiet(1000)
                        continue
                    }
                    sleepQuiet(500)
                    if (!stillActive(session)) break
                    val opened = startMicSession()
                    if (opened != null) {
                        adoptRecorder(opened.first, opened.second, opened.third)
                        useMic = true
                        ring.clear()
                        rows.reset()
                        postStatus("Mic recovered via $activeLabel @ ${sampleRate} Hz")
                    }
                    continue
                } else {
                    // Demo: push ~hop samples of a sine at wall-clock pace.
                    val sr = sampleRate.toDouble()
                    val f = demoHz.toDouble()
                    val count = hop.coerceAtLeast(64).coerceAtMost(demoScratch.size)
                    for (i in 0 until count) {
                        demoScratch[i] = (0.25 * sin(phase)).toFloat()
                        phase += 2.0 * PI * f / sr
                        if (phase > 2.0 * PI) phase -= 2.0 * PI
                    }
                    ring.write(demoScratch, 0, count)
                    sleepQuiet((1000L * count / sampleRate).coerceAtLeast(4L))
                }

                // One frame per hop of new samples; reads are hop-sized, so usually 1.
                while (stillActive(session) && ring.available >= size) {
                    if (!ring.takeWindow(re, size, hop)) break
                    try {
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
                        val row = rows.add(smoothed, plan.framesPerRow)
                        val frame = SpectrumFrame(
                            db = smoothed.copyOf(),
                            sampleRate = sampleRate,
                            fftSize = size,
                            peakHz = peakHz,
                            peakDb = peakDb,
                            rmsDb = rmsDb,
                            sourceLabel = activeLabel,
                            waterfallRow = row
                        )
                        postFrame(session, frame)
                    } catch (e: Exception) {
                        lastError = "FFT error: ${e.message}"
                        postStatus(lastError!!)
                        // Skip this hop; keep capturing.
                        break
                    }
                }
            }
        } catch (e: Exception) {
            lastError = "Capture loop error: ${e.message}"
            postStatus(lastError!!)
        } finally {
            val r = recorder
            recorder = null
            activeRecorder.compareAndSet(r, null)
            releaseRecorder(r)
        }
    }

    private fun sleepQuiet(ms: Long) {
        if (ms <= 0) return
        try {
            Thread.sleep(ms)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }
}

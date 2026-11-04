package works.squirrel.sound

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.max
import kotlin.math.min

/**
 * Spectroid-like spectrum trace + scrolling waterfall.
 * Waterfall is a scrolled ARGB bitmap (one row per hop) so redraws stay cheap.
 */
class SpectrumView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val spectrumPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#50DCFF")
        strokeWidth = 2.5f
        style = Paint.Style.STROKE
    }
    private val holdPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#FFB43C")
        strokeWidth = 1.5f
        style = Paint.Style.STROKE
        alpha = 180
    }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#243048")
        strokeWidth = 1f
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#9AA8C7")
        textSize = 28f
    }
    private val axisPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#6E7C9A")
        textSize = 22f
    }
    private val wfPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    private var db: FloatArray? = null
    private var hold: FloatArray? = null
    private var sampleRate = 44100
    private var peakHz = 0f
    private var peakDb = Levels.FLOOR_DB
    private var rmsDb = Levels.FLOOR_DB
    private var maxHoldEnabled = false

    private var wfBitmap: Bitmap? = null
    private var wfPixels: IntArray? = null
    private var wfWidth = 0
    private val wfRows = SpectrumControls.WATERFALL_ROWS

    /** Seconds the LOFARgram covers top to bottom; drives the time-axis labels. */
    var waterfallSeconds: Float = SpectrumControls.WaterfallDuration.DEFAULT.seconds
        set(value) {
            field = value
            invalidate()
        }

    /** Scroll direction of the LOFARgram; applies to rows already drawn too. */
    var direction: SpectrumControls.WaterfallDirection = SpectrumControls.WaterfallDirection.DEFAULT
        set(value) {
            field = value
            invalidate()
        }

    private val spectrumPath = Path()

    /** Visible frequency window (Hz). Defaults = human hearing (phone-friendly). */
    var fMin = SpectrumControls.DEFAULT_FMIN_HZ
    var fMax = SpectrumControls.DEFAULT_FMAX_HZ

    /** dBFS display range; [autoRange] moves it with the signal. */
    var dbMin = Levels.DEFAULT_MIN_DB
        private set
    var dbMax = Levels.DEFAULT_MAX_DB
        private set

    /** When on, the display range follows the noise floor and recent peaks. */
    var autoRange = false
        set(value) {
            field = value
            if (value) {
                autoRanger.reset()
                lastFrameNs = 0L
            }
        }
    private val autoRanger = AutoRange()
    private var lastFrameNs = 0L

    /** Fixed display range (dBFS); clamped to FLOOR_DB..0 with a minimum span. */
    fun setDbRange(minDb: Float, maxDb: Float) {
        val (lo, hi) = Levels.clampRange(minDb, maxDb)
        dbMin = lo
        dbMax = hi
        invalidate()
    }

    private var pinchStartSpan = 0f
    private var pinchStartMin = 0f
    private var pinchStartMax = 0f
    private var panStartX = 0f
    private var panStartMin = 0f
    private var panStartMax = 0f
    private var panning = false

    fun nyquist(): Float = (sampleRate / 2f).coerceAtLeast(SpectrumControls.MIN_SPAN_HZ + 1f)

    fun setMaxHold(on: Boolean) {
        maxHoldEnabled = on
        if (!on) hold = null
    }

    fun resetHold() {
        hold = null
    }

    fun clearWaterfall() {
        val pix = wfPixels
        if (pix != null) {
            pix.fill(Color.BLACK)
            wfBitmap?.setPixels(pix, 0, wfWidth, 0, 0, wfWidth, wfRows)
        }
        invalidate()
    }

    fun setFrequencyRange(minHz: Float, maxHz: Float) {
        val (lo, hi) = SpectrumControls.clampRange(minHz, maxHz, nyquist())
        fMin = lo
        fMax = hi
        invalidate()
    }

    fun applyPreset(preset: SpectrumControls.RangePreset) {
        val (lo, hi) = preset.resolve(nyquist())
        fMin = lo
        fMax = hi
        invalidate()
    }

    /** Zoom about midpoint; factor &gt;1 zooms in. */
    fun zoomBy(factor: Float) {
        val (lo, hi) = SpectrumControls.zoom(fMin, fMax, factor, nyquist())
        fMin = lo
        fMax = hi
        invalidate()
    }

    fun update(frame: SpectrumFrame) {
        if (frame.db.size < 2 || frame.sampleRate <= 0) return
        sampleRate = frame.sampleRate
        peakHz = frame.peakHz
        peakDb = frame.peakDb
        rmsDb = frame.rmsDb
        db = frame.db
        // Keep fMax from exceeding Nyquist after sample-rate changes.
        val (lo, hi) = SpectrumControls.clampRange(fMin, fMax, nyquist())
        fMin = lo
        fMax = hi
        if (autoRange) {
            val now = System.nanoTime()
            val dt = if (lastFrameNs == 0L) 0f else ((now - lastFrameNs) / 1e9f).coerceIn(0f, 1f)
            lastFrameNs = now
            val n = frame.db.size
            val ny = nyquist()
            val b0 = ((max(0f, fMin) / ny) * n).toInt().coerceIn(0, n - 2)
            val b1 = ((min(ny, fMax) / ny) * n).toInt().coerceIn(b0 + 1, n)
            val (lo, hi) = autoRanger.update(frame.db.copyOfRange(b0, b1), dt)
            dbMin = lo
            dbMax = hi
        }
        if (maxHoldEnabled) {
            val h = hold
            if (h == null || h.size != frame.db.size) {
                hold = frame.db.copyOf()
            } else {
                for (i in frame.db.indices) if (frame.db[i] > h[i]) h[i] = frame.db[i]
            }
        }
        try {
            frame.waterfallRow?.let { pushWaterfallRow(it) }
        } catch (_: Exception) {
            // Bitmap / size races during layout — skip this row.
        }
        invalidate()
    }

    /** Legacy overload used by older call sites / tests. */
    fun update(frame: FloatArray, sr: Int, peak: Float) {
        update(
            SpectrumFrame(
                db = frame,
                sampleRate = sr,
                fftSize = frame.size * 2,
                peakHz = peak,
                peakDb = frame.maxOrNull() ?: Levels.FLOOR_DB,
                rmsDb = Levels.FLOOR_DB,
                sourceLabel = "legacy",
                waterfallRow = frame
            )
        )
    }

    private fun ensureWaterfall(widthPx: Int) {
        if (widthPx <= 0) return
        if (wfBitmap != null && wfWidth == widthPx) return
        wfWidth = widthPx
        wfPixels = IntArray(widthPx * wfRows) { Color.BLACK }
        wfBitmap = Bitmap.createBitmap(widthPx, wfRows, Bitmap.Config.ARGB_8888).also {
            it.setPixels(wfPixels!!, 0, widthPx, 0, 0, widthPx, wfRows)
        }
    }

    private fun pushWaterfallRow(frame: FloatArray) {
        val w = width
        if (w <= 0) return
        val n = frame.size
        if (n < 2) return
        val ny = nyquist()
        if (ny <= 0f) return
        ensureWaterfall(w)
        val pix = wfPixels ?: return
        val bmp = wfBitmap ?: return
        if (wfWidth <= 0 || pix.size < wfWidth * wfRows) return
        // Scroll existing rows down by one.
        System.arraycopy(pix, 0, pix, wfWidth, wfWidth * (wfRows - 1))
        val lo = max(0f, fMin)
        val hi = min(ny, fMax).coerceAtLeast(lo + 1f)
        val b0 = ((lo / ny) * n).toInt().coerceIn(0, n - 2)
        val b1 = ((hi / ny) * n).toInt().coerceIn(b0 + 1, n)
        for (x in 0 until wfWidth) {
            val bin = b0 + ((b1 - b0) * x) / wfWidth
            pix[x] = dbToColor(frame[bin.coerceIn(0, n - 1)])
        }
        bmp.setPixels(pix, 0, wfWidth, 0, 0, wfWidth, wfRows)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w != oldw) ensureWaterfall(w)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        canvas.drawColor(Color.parseColor("#0B1020"))
        val spectrumH = h / 3f
        val data = db ?: return
        val n = data.size
        if (n < 2) return
        val ny = nyquist()
        if (ny <= 0f) return
        val lo = max(0f, fMin)
        val hi = min(ny, fMax).coerceAtLeast(lo + 1f)
        val b0 = ((lo / ny) * n).toInt().coerceIn(0, n - 2)
        val b1 = ((hi / ny) * n).toInt().coerceIn(b0 + 1, n)
        val span = (dbMax - dbMin).coerceAtLeast(1f)

        fun xOf(bin: Int): Float =
            ((bin - b0).toFloat() / (b1 - b0).toFloat()) * w

        fun yOf(v: Float): Float {
            val t = ((v - dbMin) / span).coerceIn(0f, 1f)
            return spectrumH - t * (spectrumH - 8f)
        }

        for (g in 0..3) {
            val dbLine = dbMin + span * g / 3f
            val y = yOf(dbLine)
            canvas.drawLine(0f, y, w, y, gridPaint)
            canvas.drawText("${dbLine.toInt()} dB", 8f, y - 4f, axisPaint)
        }

        spectrumPath.reset()
        spectrumPath.moveTo(xOf(b0), yOf(data[b0]))
        for (i in b0 + 1 until b1) {
            spectrumPath.lineTo(xOf(i), yOf(data[i]))
        }
        canvas.drawPath(spectrumPath, spectrumPaint)

        val hd = hold
        if (maxHoldEnabled && hd != null && hd.size == data.size) {
            spectrumPath.reset()
            spectrumPath.moveTo(xOf(b0), yOf(hd[b0]))
            for (i in b0 + 1 until b1) {
                spectrumPath.lineTo(xOf(i), yOf(hd[i]))
            }
            canvas.drawPath(spectrumPath, holdPaint)
        }

        canvas.drawText(
            "peak ${"%.1f".format(peakHz)} Hz (${"%.0f".format(peakDb)} dBFS) · " +
                "rms ${"%.0f".format(rmsDb)} · ${lo.toInt()}–${hi.toInt()} Hz",
            16f,
            36f,
            textPaint
        )

        val ticks = floatArrayOf(
            20f, 50f, 100f, 250f, 500f, 1000f, 2000f, 4000f, 8000f, 16000f, 20000f
        )
        for (hz in ticks) {
            if (hz < lo || hz > hi) continue
            val bin = ((hz / ny) * n).toInt().coerceIn(b0, b1)
            val x = xOf(bin)
            canvas.drawLine(x, spectrumH - 6f, x, spectrumH, gridPaint)
            val label = when {
                hz >= 1000f -> "${(hz / 1000f).let { if (it == it.toInt().toFloat()) it.toInt().toString() else "%.1f".format(it) }}k"
                else -> "${hz.toInt()}"
            }
            canvas.drawText(label, x + 2f, spectrumH - 8f, axisPaint)
        }

        val wfTop = spectrumH + 8f
        val wfH = h - wfTop
        val bmp = wfBitmap
        if (bmp != null && wfH > 0f) {
            // The bitmap always stores the newest row at the top; Fountain draws it
            // mirrored so the newest row sits at the bottom.
            val fountain = direction == SpectrumControls.WaterfallDirection.FOUNTAIN
            if (fountain) {
                canvas.save()
                canvas.scale(1f, -1f, w / 2f, wfTop + wfH / 2f)
            }
            canvas.drawBitmap(bmp, null, RectF(0f, wfTop, w, h), wfPaint)
            if (fountain) canvas.restore()
            // Time axis: 0 at the newest edge, the full span at the oldest edge.
            for (k in 0..4) {
                val frac = direction.yFraction(k / 4f)
                val y = wfTop + wfH * frac
                val label = SpectrumControls.timeLabel(waterfallSeconds * k / 4f)
                val ty = (y + if (frac == 0f) 28f else -6f).coerceAtMost(h - 6f)
                canvas.drawLine(0f, y, 14f, y, gridPaint)
                canvas.drawText(label, 18f, ty, axisPaint)
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        try {
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    panStartX = event.x
                    panStartMin = fMin
                    panStartMax = fMax
                    panning = true
                }
                MotionEvent.ACTION_POINTER_DOWN -> {
                    if (event.pointerCount >= 2) {
                        panning = false
                        pinchStartSpan = span(event)
                        pinchStartMin = fMin
                        pinchStartMax = fMax
                    }
                }
                MotionEvent.ACTION_MOVE -> {
                    if (event.pointerCount >= 2 && pinchStartSpan > 1f) {
                        val s = span(event) / pinchStartSpan
                        if (s > 0.01f) {
                            val (lo, hi) = SpectrumControls.zoom(
                                pinchStartMin,
                                pinchStartMax,
                                s,
                                nyquist()
                            )
                            fMin = lo
                            fMax = hi
                            invalidate()
                        }
                    } else if (panning && event.pointerCount == 1 && width > 0) {
                        val dx = event.x - panStartX
                        val frac = -dx / width.toFloat()
                        val (lo, hi) = SpectrumControls.pan(
                            panStartMin,
                            panStartMax,
                            frac,
                            nyquist()
                        )
                        fMin = lo
                        fMax = hi
                        invalidate()
                    }
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    panning = false
                    pinchStartSpan = 0f
                }
            }
        } catch (_: Exception) {
            // Pointer index races — ignore the gesture frame.
        }
        return true
    }

    private fun span(e: MotionEvent): Float {
        if (e.pointerCount < 2) return 0f
        val dx = e.getX(0) - e.getX(1)
        val dy = e.getY(0) - e.getY(1)
        return kotlin.math.sqrt(dx * dx + dy * dy)
    }

    private fun dbToColor(dbVal: Float): Int = Levels.colorFor(dbVal, dbMin, dbMax)
}

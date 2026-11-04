package works.squirrel.sound

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.max
import kotlin.math.min

/**
 * Live spectrum (top) and LOFARgram waterfall (bottom), split by [splitRatio].
 * The waterfall is a ring of bitmap rows, one per screen pixel, so redraws stay cheap.
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

    // LOFARgram: a ring of bitmap rows, one row per screen pixel of the waterfall
    // area. [wfHead] is the newest row; older rows follow it (wrapping), so a new
    // row costs one bitmap row write instead of scrolling the whole bitmap.
    private var wfBitmap: Bitmap? = null
    private var wfRowPixels: IntArray? = null
    private var wfWidth = 0
    private var wfHead = 0

    /** Rows in the LOFARgram bitmap (= its pixel height once laid out). */
    var waterfallRows: Int = SpectrumControls.WATERFALL_ROWS
        private set

    /** Called when [waterfallRows] changes, so the engine can plan its rows. */
    var onWaterfallRowsChanged: ((Int) -> Unit)? = null

    /** Share of the height given to the spectrum (top) vs the LOFARgram (bottom). */
    var splitRatio: SpectrumControls.SplitRatio = SpectrumControls.SplitRatio.DEFAULT
        set(value) {
            if (field == value) return
            field = value
            ensureWaterfall(width, height)
            invalidate()
        }

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
    private val srcRect = Rect()
    private val dstRect = RectF()

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
        wfBitmap?.eraseColor(Color.BLACK)
        wfHead = 0
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

    /** (Re)build the LOFARgram ring when the width or the waterfall's pixel height changes. */
    private fun ensureWaterfall(widthPx: Int, heightPx: Int) {
        if (widthPx <= 0 || heightPx <= 0) return
        val layout = SpectrumControls.splitLayout(heightPx, splitRatio)
        val rows = SpectrumControls.waterfallRowsFor(layout.waterfallHeight)
        if (wfBitmap != null && wfWidth == widthPx && waterfallRows == rows) return
        wfBitmap?.recycle()
        wfWidth = widthPx
        wfRowPixels = IntArray(widthPx)
        wfBitmap = Bitmap.createBitmap(widthPx, rows, Bitmap.Config.ARGB_8888).also {
            it.eraseColor(Color.BLACK)
        }
        wfHead = 0
        if (rows != waterfallRows) {
            waterfallRows = rows
            onWaterfallRowsChanged?.invoke(rows)
        }
    }

    private fun pushWaterfallRow(frame: FloatArray) {
        val w = width
        if (w <= 0) return
        val n = frame.size
        if (n < 2) return
        val ny = nyquist()
        if (ny <= 0f) return
        ensureWaterfall(w, height)
        val row = wfRowPixels ?: return
        val bmp = wfBitmap ?: return
        if (wfWidth <= 0 || row.size < wfWidth) return
        val lo = max(0f, fMin)
        val hi = min(ny, fMax).coerceAtLeast(lo + 1f)
        val b0 = ((lo / ny) * n).toInt().coerceIn(0, n - 2)
        val b1 = ((hi / ny) * n).toInt().coerceIn(b0 + 1, n)
        for (x in 0 until wfWidth) {
            val bin = b0 + ((b1 - b0) * x) / wfWidth
            row[x] = dbToColor(frame[bin.coerceIn(0, n - 1)])
        }
        // New row goes just before the previous newest (age grows with index).
        wfHead = Math.floorMod(wfHead - 1, waterfallRows)
        bmp.setPixels(row, 0, wfWidth, 0, wfHead, wfWidth, 1)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        ensureWaterfall(w, h)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        canvas.drawColor(Color.parseColor("#0B1020"))
        val split = SpectrumControls.splitLayout(height, splitRatio)
        val spectrumH = split.spectrumHeight.toFloat()
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

        val wfTop = split.waterfallTop.toFloat()
        val wfH = split.waterfallHeight.toFloat()
        val bmp = wfBitmap
        if (bmp != null && wfH > 0f) {
            // Rows are laid out newest-first from the ring head; Fountain draws the
            // same picture mirrored so the newest row sits at the bottom.
            val fountain = direction == SpectrumControls.WaterfallDirection.FOUNTAIN
            if (fountain) {
                canvas.save()
                canvas.scale(1f, -1f, w / 2f, wfTop + wfH / 2f)
            }
            val rows = bmp.height
            val rowH = wfH / rows // 1 px per row once laid out
            var y = wfTop
            for (seg in SpectrumControls.ringSegments(wfHead, rows)) {
                val count = seg.last - seg.first + 1
                srcRect.set(0, seg.first, bmp.width, seg.last + 1)
                dstRect.set(0f, y, w, y + count * rowH)
                canvas.drawBitmap(bmp, srcRect, dstRect, wfPaint)
                y += count * rowH
            }
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

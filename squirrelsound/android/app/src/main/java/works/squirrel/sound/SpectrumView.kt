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
 * Live spectrum (or scope) and LOFARgram waterfall, split by [splitRatio].
 * Portrait: spectrum on top, LOFARgram below. Landscape: spectrum left, LOFARgram right.
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
    private val barPaint = Paint().apply {
        color = Color.parseColor("#3A9FC0")
        style = Paint.Style.FILL
    }
    private val barHoldPaint = Paint().apply {
        color = Color.parseColor("#FFB43C")
        strokeWidth = 3f
    }
    private val scopePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#7CFC9A")
        strokeWidth = 1.5f
        style = Paint.Style.STROKE
    }
    private val peakMarkPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#FFE08A")
        style = Paint.Style.FILL
        textSize = 22f
    }

    private var db: FloatArray? = null
    private var hold: FloatArray? = null
    private var time: FloatArray? = null
    private var fftSize = 0
    private var sampleRate = 44100
    private var peakHz = 0f
    private var peakDb = Levels.FLOOR_DB
    private var rmsDb = Levels.FLOOR_DB
    private var maxHoldEnabled = false

    /**
     * When on, the spectrum pane shows a triggered time-domain trace instead of
     * the FFT / band view (Settings switch).
     */
    var scopeEnabled: Boolean = false
        set(value) {
            if (field == value) return
            field = value
            invalidate()
        }

    /** Label the loudest peaks on the live or held spectrum (via [SqDsp.peaks]). */
    var peaksEnabled: Boolean = true
        set(value) {
            if (field == value) return
            field = value
            invalidate()
        }

    /** Spectrum line, or octave / third-octave bars (computed with libs/sqdsp). */
    var viewMode: SpectrumControls.ViewMode = SpectrumControls.ViewMode.DEFAULT
        set(value) {
            if (field == value) return
            field = value
            bandSet = null
            bandHold = null
            invalidate()
        }
    private var bandSet: Bands.Set? = null
    private var bandKey = ""
    private var bandDb = FloatArray(0)
    private var bandHold: FloatArray? = null
    private var enbwSize = 0
    private var enbw = 1.5

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

    /** Share given to the spectrum pane vs the LOFARgram (same ratios in both orientations). */
    var splitRatio: SpectrumControls.SplitRatio = SpectrumControls.SplitRatio.DEFAULT
        set(value) {
            if (field == value) return
            field = value
            ensureWaterfall(width, height)
            invalidate()
        }

    private fun currentSplit(w: Int = width, h: Int = height): SpectrumControls.SplitLayout {
        val land = SpectrumControls.isLandscape(w, h)
        return SpectrumControls.splitLayout(w, h, splitRatio, land)
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
        if (!on) {
            hold = null
            bandHold = null
        }
    }

    fun resetHold() {
        hold = null
        bandHold = null
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
        fftSize = frame.fftSize
        peakHz = frame.peakHz
        peakDb = frame.peakDb
        rmsDb = frame.rmsDb
        db = frame.db
        time = frame.time
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
            val (alo, ahi) = autoRanger.update(frame.db.copyOfRange(b0, b1), dt)
            dbMin = alo
            dbMax = ahi
        }
        if (maxHoldEnabled) {
            val h = hold
            if (h == null || h.size != frame.db.size) {
                hold = frame.db.copyOf()
            } else {
                for (i in frame.db.indices) if (frame.db[i] > h[i]) h[i] = frame.db[i]
            }
        }
        if (viewMode.isBands && !scopeEnabled) updateBands(frame)
        try {
            frame.waterfallRow?.let { pushWaterfallRow(it) }
        } catch (_: Exception) {
            // Bitmap / size races during layout — skip this row.
        }
        invalidate()
    }

    /** Band levels of this frame over the visible range (bands rebuilt when the range changes). */
    private fun updateBands(frame: SpectrumFrame) {
        val key = "${viewMode.fraction}:${fMin.toInt()}:${fMax.toInt()}"
        var set = bandSet
        if (set == null || key != bandKey) {
            set = SqDsp.octaveBands(viewMode.fraction, fMin.toDouble(), fMax.toDouble())
            bandSet = set
            bandKey = key
            bandHold = null
        }
        if (enbwSize != frame.fftSize) {
            enbw = Bands.enbw(Fft.hann(frame.fftSize))
            enbwSize = frame.fftSize
        }
        if (bandDb.size != set.size) bandDb = FloatArray(set.size)
        SqDsp.bandLevels(frame.db, frame.sampleRate.toDouble() / frame.fftSize, set, enbw, bandDb)
        if (maxHoldEnabled) {
            val h = bandHold
            if (h == null || h.size != bandDb.size) {
                bandHold = bandDb.copyOf()
            } else {
                for (i in bandDb.indices) if (bandDb[i] > h[i]) h[i] = bandDb[i]
            }
        }
    }

    /** Bars for the band levels across the spectrum pane, labelled under them. */
    private fun drawBands(
        canvas: Canvas,
        sx: Float,
        sy: Float,
        sw: Float,
        sh: Float,
        yOf: (Float) -> Float,
    ) {
        val set = bandSet ?: return
        val n = minOf(set.size, bandDb.size)
        if (n == 0) return
        val per = sw / n
        val gap = (per * 0.15f).coerceIn(1f, 6f)
        val base = yOf(dbMin)
        val stride = SpectrumControls.labelStride(n, sw, axisPaint.measureText("12.5k") + 8f)
        val hd = bandHold
        for (i in 0 until n) {
            val x0 = sx + i * per + gap / 2
            val x1 = sx + (i + 1) * per - gap / 2
            canvas.drawRect(x0, yOf(bandDb[i]), x1, base, barPaint)
            if (maxHoldEnabled && hd != null && i < hd.size) {
                val y = yOf(hd[i])
                canvas.drawLine(x0, y, x1, y, barHoldPaint)
            }
            if (i % stride == 0) {
                val label = Bands.label(set.centre[i])
                val lx = (x0 + x1) / 2 - axisPaint.measureText(label) / 2
                canvas.drawText(label, lx.coerceAtLeast(sx), sy + sh - 8f, axisPaint)
            }
        }
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

    /** (Re)build the LOFARgram ring when the waterfall pane's pixel size changes. */
    private fun ensureWaterfall(widthPx: Int, heightPx: Int) {
        if (widthPx <= 0 || heightPx <= 0) return
        val layout = currentSplit(widthPx, heightPx)
        val paneW = layout.waterfallWidth.coerceAtLeast(1)
        val rows = SpectrumControls.waterfallRowsFor(layout.waterfallHeight)
        if (wfBitmap != null && wfWidth == paneW && waterfallRows == rows) return
        wfBitmap?.recycle()
        wfWidth = paneW
        wfRowPixels = IntArray(paneW)
        wfBitmap = Bitmap.createBitmap(paneW, rows, Bitmap.Config.ARGB_8888).also {
            it.eraseColor(Color.BLACK)
        }
        wfHead = 0
        if (rows != waterfallRows) {
            waterfallRows = rows
            onWaterfallRowsChanged?.invoke(rows)
        }
    }

    private fun pushWaterfallRow(frame: FloatArray) {
        if (width <= 0 || height <= 0) return
        val n = frame.size
        if (n < 2) return
        val ny = nyquist()
        if (ny <= 0f) return
        ensureWaterfall(width, height)
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
        val viewH = height.toFloat()
        canvas.drawColor(Color.parseColor("#0B1020"))
        val split = currentSplit()
        val sx = split.spectrumLeft.toFloat()
        val sy = split.spectrumTop.toFloat()
        val sw = split.spectrumWidth.toFloat().coerceAtLeast(1f)
        val sh = split.spectrumHeight.toFloat().coerceAtLeast(1f)
        val data = db ?: return
        val n = data.size
        if (n < 2) return
        val ny = nyquist()
        if (ny <= 0f) return
        val lo = max(0f, fMin)
        val hi = min(ny, fMax).coerceAtLeast(lo + 1f)

        if (scopeEnabled) {
            drawScope(canvas, sx, sy, sw, sh)
        } else {
            drawSpectrumPane(canvas, data, n, ny, lo, hi, sx, sy, sw, sh)
        }

        canvas.drawText(
            "peak ${"%.1f".format(peakHz)} Hz (${"%.0f".format(peakDb)} dBFS) · " +
                "rms ${"%.0f".format(rmsDb)} · ${lo.toInt()}–${hi.toInt()} Hz",
            sx + 16f,
            sy + 36f,
            textPaint
        )

        val wfLeft = split.waterfallLeft.toFloat()
        val wfTop = split.waterfallTop.toFloat()
        val wfW = split.waterfallWidth.toFloat()
        val wfH = split.waterfallHeight.toFloat()
        val bmp = wfBitmap
        if (bmp != null && wfH > 0f && wfW > 0f) {
            // Rows are laid out newest-first from the ring head; Fountain draws the
            // same picture mirrored so the newest row sits at the bottom.
            val fountain = direction == SpectrumControls.WaterfallDirection.FOUNTAIN
            if (fountain) {
                canvas.save()
                canvas.scale(1f, -1f, wfLeft + wfW / 2f, wfTop + wfH / 2f)
            }
            val rows = bmp.height
            val rowH = wfH / rows // 1 px per row once laid out
            var drawY = wfTop
            for (seg in SpectrumControls.ringSegments(wfHead, rows)) {
                val count = seg.last - seg.first + 1
                srcRect.set(0, seg.first, bmp.width, seg.last + 1)
                dstRect.set(wfLeft, drawY, wfLeft + wfW, drawY + count * rowH)
                canvas.drawBitmap(bmp, srcRect, dstRect, wfPaint)
                drawY += count * rowH
            }
            if (fountain) canvas.restore()
            // Time axis: 0 at the newest edge, the full span at the oldest edge.
            for (k in 0..4) {
                val frac = direction.yFraction(k / 4f)
                val tickY = wfTop + wfH * frac
                val label = SpectrumControls.timeLabel(waterfallSeconds * k / 4f)
                val ty = (tickY + if (frac == 0f) 28f else -6f).coerceAtMost(viewH - 6f)
                canvas.drawLine(wfLeft, tickY, wfLeft + 14f, tickY, gridPaint)
                canvas.drawText(label, wfLeft + 18f, ty, axisPaint)
            }
        }
    }

    private fun drawSpectrumPane(
        canvas: Canvas,
        data: FloatArray,
        n: Int,
        ny: Float,
        lo: Float,
        hi: Float,
        sx: Float,
        sy: Float,
        sw: Float,
        sh: Float,
    ) {
        val b0 = ((lo / ny) * n).toInt().coerceIn(0, n - 2)
        val b1 = ((hi / ny) * n).toInt().coerceIn(b0 + 1, n)
        val span = (dbMax - dbMin).coerceAtLeast(1f)

        fun xOf(bin: Int): Float =
            sx + ((bin - b0).toFloat() / (b1 - b0).toFloat()) * sw

        fun yOf(v: Float): Float {
            val t = ((v - dbMin) / span).coerceIn(0f, 1f)
            return sy + sh - t * (sh - 8f)
        }

        for (g in 0..3) {
            val dbLine = dbMin + span * g / 3f
            val y = yOf(dbLine)
            canvas.drawLine(sx, y, sx + sw, y, gridPaint)
            val label = "${dbLine.toInt()} dB"
            val ly = when (g) {
                3 -> y + axisPaint.textSize + 2f
                0 -> y - axisPaint.textSize - 12f
                else -> y - 4f
            }
            canvas.drawText(label, sx + sw - 8f - axisPaint.measureText(label), ly, axisPaint)
        }

        val bands = viewMode.isBands
        if (bands) {
            drawBands(canvas, sx, sy, sw, sh, ::yOf)
        } else {
            spectrumPath.reset()
            spectrumPath.moveTo(xOf(b0), yOf(data[b0]))
            for (i in b0 + 1 until b1) {
                spectrumPath.lineTo(xOf(i), yOf(data[i]))
            }
            canvas.drawPath(spectrumPath, spectrumPaint)
        }

        val hd = hold
        val holdSpec = !bands && maxHoldEnabled && hd != null && hd.size == data.size
        if (holdSpec) {
            spectrumPath.reset()
            spectrumPath.moveTo(xOf(b0), yOf(hd!![b0]))
            for (i in b0 + 1 until b1) {
                spectrumPath.lineTo(xOf(i), yOf(hd[i]))
            }
            canvas.drawPath(spectrumPath, holdPaint)
        }

        if (!bands && peaksEnabled) {
            val spec = if (holdSpec) hd!! else data
            drawPeakLabels(canvas, spec, n, ny, lo, hi, sx, sy, sw, sh, ::xOf, ::yOf)
        }

        val ticks = floatArrayOf(
            20f, 50f, 100f, 250f, 500f, 1000f, 2000f, 4000f, 8000f, 16000f, 20000f
        )
        for (hz in ticks) {
            if (bands || hz < lo || hz > hi) continue
            val bin = ((hz / ny) * n).toInt().coerceIn(b0, b1)
            val x = xOf(bin)
            canvas.drawLine(x, sy + sh - 6f, x, sy + sh, gridPaint)
            val label = when {
                hz >= 1000f -> "${(hz / 1000f).let { if (it == it.toInt().toFloat()) it.toInt().toString() else "%.1f".format(it) }}k"
                else -> "${hz.toInt()}"
            }
            canvas.drawText(label, x + 2f, sy + sh - 8f, axisPaint)
        }
    }

    /** Triggered min-max scope of the last unwindowed frame (desktop Live twin). */
    private fun drawScope(canvas: Canvas, sx: Float, sy: Float, sw: Float, sh: Float) {
        val samples = time ?: return
        val n = samples.size
        if (n < 2) return
        val span = ScopeTrigger.autoSpan(if (fftSize > 0) fftSize else n)
        val start = ScopeTrigger.risingEdgeStart(samples, span)
        val mid = sy + sh / 2f
        canvas.drawLine(sx, mid, sx + sw, mid, gridPaint)
        canvas.drawLine(sx, sy + sh / 4f, sx + sw, sy + sh / 4f, gridPaint)
        canvas.drawLine(sx, sy + 3f * sh / 4f, sx + sw, sy + 3f * sh / 4f, gridPaint)
        val half = sh / 2f - 1f
        var prev = mid
        for (x in 0 until sw.toInt().coerceAtLeast(1)) {
            val a = start + span * x / sw.toInt()
            val b = start + span * (x + 1) / sw.toInt()
            val from = a.coerceIn(0, n - 1)
            val to = b.coerceAtLeast(from + 1).coerceAtMost(n)
            var lo = 1e9f
            var hi = -1e9f
            for (i in from until to) {
                val v = samples[i]
                if (v < lo) lo = v
                if (v > hi) hi = v
            }
            val y0 = mid - hi * half
            val y1 = mid - lo * half
            val top = min(y0, prev)
            val bot = max(y1, prev)
            val px = sx + x
            canvas.drawLine(px, top, px, bot, scopePaint)
            prev = mid - (lo + hi) / 2f * half
        }
        val ms = ScopeTrigger.spanMs(span, sampleRate)
        canvas.drawText("+1", sx + 2f, sy + 14f, axisPaint)
        canvas.drawText("-1", sx + 2f, sy + sh - 6f, axisPaint)
        val msLabel = "%.1f ms".format(ms)
        canvas.drawText(msLabel, sx + sw - 8f - axisPaint.measureText(msLabel), sy + 14f, axisPaint)
    }

    private fun drawPeakLabels(
        canvas: Canvas,
        spec: FloatArray,
        n: Int,
        ny: Float,
        lo: Float,
        hi: Float,
        sx: Float,
        sy: Float,
        sw: Float,
        @Suppress("UNUSED_PARAMETER") sh: Float,
        xOf: (Int) -> Float,
        yOf: (Float) -> Float,
    ) {
        val size = if (fftSize > 0) fftSize else (n - 1) * 2
        val peaks = SqDsp.peaks(
            magDb = spec,
            sampleRate = sampleRate.toDouble(),
            fftSize = size,
            fMin = lo.toDouble(),
            fMax = hi.toDouble(),
            dbMin = dbMin.toDouble(),
        )
        for (p in peaks) {
            if (p.hz < lo || p.hz > hi) continue
            val bin = ((p.hz / ny) * n).toInt().coerceIn(0, n - 1)
            val x = xOf(bin)
            val y = yOf(p.db.toFloat())
            // Small downward triangle above the peak.
            spectrumPath.reset()
            spectrumPath.moveTo(x, y - 4f)
            spectrumPath.lineTo(x - 6f, y - 14f)
            spectrumPath.lineTo(x + 6f, y - 14f)
            spectrumPath.close()
            canvas.drawPath(spectrumPath, peakMarkPaint)
            val label = if (p.hz >= 1000) {
                "%.2f kHz %.0f dB".format(p.hz / 1000.0, p.db)
            } else {
                "%.0f Hz %.0f dB".format(p.hz, p.db)
            }
            val tw = peakMarkPaint.measureText(label)
            val tx = (x - tw / 2f).coerceIn(sx, sx + sw - tw)
            val ty = (y - 18f).coerceAtLeast(sy + peakMarkPaint.textSize)
            canvas.drawText(label, tx, ty, peakMarkPaint)
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
                        val paneW = currentSplit().spectrumWidth.coerceAtLeast(1)
                        val dx = event.x - panStartX
                        val frac = -dx / paneW.toFloat()
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

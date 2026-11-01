package works.squirrel.sound

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.max
import kotlin.math.min

class SpectrumView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val spectrumPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#50DCFF")
        strokeWidth = 2f
        style = Paint.Style.STROKE
    }
    private val holdPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#FFB43C")
        strokeWidth = 1.5f
        style = Paint.Style.STROKE
        alpha = 180
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#9AA8C7")
        textSize = 28f
    }
    private val wfPaint = Paint()

    private var db: FloatArray? = null
    private var hold: FloatArray? = null
    private var sampleRate = 44100
    private var peakHz = 0f
    private var maxHoldEnabled = false

    private val waterfall = ArrayDeque<IntArray>()
    private val wfRows = 120

    /** Visible frequency window (Hz). */
    var fMin = 20f
    var fMax = 8000f

    private var pinchStartSpan = 0f
    private var pinchStartMin = 0f
    private var pinchStartMax = 0f

    fun setMaxHold(on: Boolean) {
        maxHoldEnabled = on
        if (!on) hold = null
    }

    fun resetHold() {
        hold = null
    }

    fun update(frame: FloatArray, sr: Int, peak: Float) {
        sampleRate = sr
        peakHz = peak
        db = frame
        if (maxHoldEnabled) {
            val h = hold
            if (h == null || h.size != frame.size) {
                hold = frame.copyOf()
            } else {
                for (i in frame.indices) if (frame[i] > h[i]) h[i] = frame[i]
            }
        }
        val row = IntArray(frame.size) { i -> dbToColor(frame[i]) }
        waterfall.addFirst(row)
        while (waterfall.size > wfRows) waterfall.removeLast()
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        canvas.drawColor(Color.parseColor("#0B1020"))
        val spectrumH = h / 3f
        val data = db ?: return
        val ny = sampleRate / 2f
        val lo = max(0f, fMin)
        val hi = min(ny, fMax)
        val n = data.size
        val b0 = ((lo / ny) * n).toInt().coerceIn(0, n - 2)
        val b1 = ((hi / ny) * n).toInt().coerceIn(b0 + 1, n)

        fun xOf(bin: Int): Float =
            ((bin - b0).toFloat() / (b1 - b0).toFloat()) * w

        fun yOf(v: Float): Float {
            val t = ((v + 90f) / 90f).coerceIn(0f, 1f)
            return spectrumH - t * (spectrumH - 8f)
        }

        var prevX = xOf(b0)
        var prevY = yOf(data[b0])
        for (i in b0 + 1 until b1) {
            val x = xOf(i)
            val y = yOf(data[i])
            canvas.drawLine(prevX, prevY, x, y, spectrumPaint)
            prevX = x
            prevY = y
        }

        val hd = hold
        if (maxHoldEnabled && hd != null && hd.size == data.size) {
            prevX = xOf(b0)
            prevY = yOf(hd[b0])
            for (i in b0 + 1 until b1) {
                val x = xOf(i)
                val y = yOf(hd[i])
                canvas.drawLine(prevX, prevY, x, y, holdPaint)
                prevX = x
                prevY = y
            }
        }

        canvas.drawText(
            "peak ${"%.1f".format(peakHz)} Hz · ${lo.toInt()}–${hi.toInt()} Hz",
            16f,
            36f,
            textPaint
        )

        val wfTop = spectrumH + 8f
        val wfH = h - wfTop
        if (waterfall.isEmpty() || wfH <= 0f) return
        val rowH = wfH / wfRows
        waterfall.forEachIndexed { ri, row ->
            val y = wfTop + ri * rowH
            for (i in b0 until b1) {
                val x0 = xOf(i)
                val x1 = xOf(i + 1)
                wfPaint.color = row[i]
                canvas.drawRect(x0, y, x1, y + rowH + 1f, wfPaint)
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_POINTER_DOWN -> {
                if (event.pointerCount >= 2) {
                    pinchStartSpan = span(event)
                    pinchStartMin = fMin
                    pinchStartMax = fMax
                }
            }
            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount >= 2 && pinchStartSpan > 1f) {
                    val s = span(event) / pinchStartSpan
                    val mid = (pinchStartMin + pinchStartMax) / 2f
                    val half = ((pinchStartMax - pinchStartMin) / 2f) / s
                    fMin = max(20f, mid - half)
                    fMax = min(sampleRate / 2f, mid + half)
                    invalidate()
                }
            }
        }
        return true
    }

    private fun span(e: MotionEvent): Float {
        val dx = e.getX(0) - e.getX(1)
        val dy = e.getY(0) - e.getY(1)
        return kotlin.math.sqrt(dx * dx + dy * dy)
    }

    private fun dbToColor(db: Float): Int {
        val t = ((db + 90f) / 90f).coerceIn(0f, 1f)
        val r = (20 + 235 * t).toInt()
        val g = (30 + 200 * t * t).toInt()
        val b = (90 + 140 * (1 - t)).toInt()
        return Color.rgb(r, g, b)
    }
}

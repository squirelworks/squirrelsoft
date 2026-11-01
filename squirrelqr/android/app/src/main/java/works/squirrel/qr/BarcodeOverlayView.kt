package works.squirrel.qr

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat

/**
 * Draws a bounding box + crosshair over the camera preview where ML Kit
 * reports a barcode. Coordinates are already mapped into view space.
 */
class BarcodeOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val boxPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = resources.displayMetrics.density * 2.5f
        color = ContextCompat.getColor(context, R.color.accent)
    }

    private val crossPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = resources.displayMetrics.density * 1.5f
        color = ContextCompat.getColor(context, R.color.ok)
    }

    private val cornerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = resources.displayMetrics.density * 3.5f
        strokeCap = Paint.Cap.ROUND
        color = ContextCompat.getColor(context, R.color.warm)
    }

    private var target: RectF? = null

    fun setTarget(rect: RectF?) {
        target = rect
        invalidate()
    }

    fun clear() {
        if (target != null) {
            target = null
            invalidate()
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val r = target ?: return
        if (r.width() < 4f || r.height() < 4f) return

        canvas.drawRect(r, boxPaint)

        val cx = r.centerX()
        val cy = r.centerY()
        val arm = minOf(r.width(), r.height()) * 0.18f
        canvas.drawLine(cx - arm, cy, cx + arm, cy, crossPaint)
        canvas.drawLine(cx, cy - arm, cx, cy + arm, crossPaint)

        val len = minOf(r.width(), r.height()) * 0.22f
        // Top-left
        canvas.drawLine(r.left, r.top, r.left + len, r.top, cornerPaint)
        canvas.drawLine(r.left, r.top, r.left, r.top + len, cornerPaint)
        // Top-right
        canvas.drawLine(r.right, r.top, r.right - len, r.top, cornerPaint)
        canvas.drawLine(r.right, r.top, r.right, r.top + len, cornerPaint)
        // Bottom-left
        canvas.drawLine(r.left, r.bottom, r.left + len, r.bottom, cornerPaint)
        canvas.drawLine(r.left, r.bottom, r.left, r.bottom - len, cornerPaint)
        // Bottom-right
        canvas.drawLine(r.right, r.bottom, r.right - len, r.bottom, cornerPaint)
        canvas.drawLine(r.right, r.bottom, r.right, r.bottom - len, cornerPaint)
    }
}

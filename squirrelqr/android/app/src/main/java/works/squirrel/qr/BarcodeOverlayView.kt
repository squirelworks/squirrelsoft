package works.squirrel.qr

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat

/**
 * Camera overlay: always-on viewfinder crosshair, plus a bounding box when ML Kit
 * reports a QR. Coordinates for [setTarget] are already mapped into view space.
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

    private val guidePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = resources.displayMetrics.density * 1.25f
        color = ContextCompat.getColor(context, R.color.muted)
        alpha = 180
    }

    private var target: RectF? = null

    fun setTarget(rect: RectF?) {
        target = rect
        invalidate()
    }

    fun clear() {
        target = null
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // Always-on viewfinder crosshair (product: camera + crosshair).
        val viewCx = width / 2f
        val viewCy = height / 2f
        val guideArm = minOf(width, height) * 0.08f
        canvas.drawLine(viewCx - guideArm, viewCy, viewCx + guideArm, viewCy, guidePaint)
        canvas.drawLine(viewCx, viewCy - guideArm, viewCx, viewCy + guideArm, guidePaint)

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

package works.squirrel.qr

import android.graphics.Rect
import android.graphics.RectF
import kotlin.math.max

/**
 * Maps an upright ML Kit barcode [Rect] (image space) into PreviewView
 * coordinates assuming [androidx.camera.view.PreviewView.ScaleType.FILL_CENTER].
 *
 * ML Kit reports boxes in the *rotated, upright* frame, while
 * `InputImage.width/height` of a media image are the unrotated sensor buffer
 * sizes. Use [uprightSize] (or [mapRectForBuffer]) so portrait phones scale and
 * centre with the right axes.
 */
object PreviewMapper {
    data class Box(val left: Float, val top: Float, val right: Float, val bottom: Float) {
        val centerX: Float get() = (left + right) / 2f
        val centerY: Float get() = (top + bottom) / 2f
    }

    /** Upright image size for a sensor buffer of [bufferW]×[bufferH] rotated by [rotationDegrees]. */
    fun uprightSize(bufferW: Int, bufferH: Int, rotationDegrees: Int): Pair<Int, Int> =
        when (((rotationDegrees % 360) + 360) % 360) {
            90, 270 -> bufferH to bufferW
            else -> bufferW to bufferH
        }

    /**
     * A point in the unrotated [bufferW]×[bufferH] buffer, in the upright image
     * (the buffer turned clockwise by [rotationDegrees], as CameraX reports it).
     */
    fun uprightPoint(x: Float, y: Float, bufferW: Int, bufferH: Int, rotationDegrees: Int): Pair<Float, Float> =
        when (((rotationDegrees % 360) + 360) % 360) {
            90 -> (bufferH - y) to x
            180 -> (bufferW - x) to (bufferH - y)
            270 -> y to (bufferW - x)
            else -> x to y
        }

    /** Upright bounding box (left, top, right, bottom) of buffer points given as x,y pairs. */
    fun uprightBounds(points: FloatArray, bufferW: Int, bufferH: Int, rotationDegrees: Int): IntArray {
        var l = Float.MAX_VALUE
        var t = Float.MAX_VALUE
        var r = -Float.MAX_VALUE
        var b = -Float.MAX_VALUE
        for (i in 0 until points.size / 2) {
            val (x, y) = uprightPoint(points[2 * i], points[2 * i + 1], bufferW, bufferH, rotationDegrees)
            l = minOf(l, x)
            t = minOf(t, y)
            r = maxOf(r, x)
            b = maxOf(b, y)
        }
        return intArrayOf(l.toInt(), t.toInt(), kotlin.math.ceil(r).toInt(), kotlin.math.ceil(b).toInt())
    }

    /** [mapBox] for a box from ML Kit on an unrotated buffer plus its rotation. */
    fun mapBoxForBuffer(
        left: Int,
        top: Int,
        right: Int,
        bottom: Int,
        bufferWidth: Int,
        bufferHeight: Int,
        rotationDegrees: Int,
        viewWidth: Int,
        viewHeight: Int
    ): Box? {
        val (w, h) = uprightSize(bufferWidth, bufferHeight, rotationDegrees)
        return mapBox(left, top, right, bottom, w, h, viewWidth, viewHeight)
    }

    fun mapBox(
        left: Int,
        top: Int,
        right: Int,
        bottom: Int,
        imageWidth: Int,
        imageHeight: Int,
        viewWidth: Int,
        viewHeight: Int
    ): Box? {
        if (imageWidth <= 0 || imageHeight <= 0 || viewWidth <= 0 || viewHeight <= 0) {
            return null
        }
        val scale = max(
            viewWidth.toFloat() / imageWidth.toFloat(),
            viewHeight.toFloat() / imageHeight.toFloat()
        )
        val scaledW = imageWidth * scale
        val scaledH = imageHeight * scale
        val dx = (viewWidth - scaledW) / 2f
        val dy = (viewHeight - scaledH) / 2f
        return Box(
            left * scale + dx,
            top * scale + dy,
            right * scale + dx,
            bottom * scale + dy
        )
    }

    fun mapRectForBuffer(
        box: Rect,
        bufferWidth: Int,
        bufferHeight: Int,
        rotationDegrees: Int,
        viewWidth: Int,
        viewHeight: Int
    ): RectF? {
        val (w, h) = uprightSize(bufferWidth, bufferHeight, rotationDegrees)
        return mapRect(box, w, h, viewWidth, viewHeight)
    }

    fun mapRect(
        box: Rect,
        imageWidth: Int,
        imageHeight: Int,
        viewWidth: Int,
        viewHeight: Int
    ): RectF? {
        val mapped = mapBox(
            box.left, box.top, box.right, box.bottom,
            imageWidth, imageHeight, viewWidth, viewHeight
        ) ?: return null
        return RectF(mapped.left, mapped.top, mapped.right, mapped.bottom)
    }
}

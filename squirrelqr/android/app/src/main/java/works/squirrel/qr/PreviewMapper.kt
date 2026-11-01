package works.squirrel.qr

import android.graphics.Rect
import android.graphics.RectF
import kotlin.math.max

/**
 * Maps an upright ML Kit barcode [Rect] (image space) into PreviewView
 * coordinates assuming [androidx.camera.view.PreviewView.ScaleType.FILL_CENTER].
 */
object PreviewMapper {
    data class Box(val left: Float, val top: Float, val right: Float, val bottom: Float)

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

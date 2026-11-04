package works.squirrel.qr

/**
 * Scan modes and the fixed framing-guide geometry (no Android types, so it is
 * unit-tested on the JVM).
 */
enum class ScanMode(val id: String) {
    /** Default: a fixed square in the centre; only codes inside it are accepted. */
    FIXED_GUIDE("fixed"),
    /** Box + crosshair that follow the detected code. */
    TRACKING("tracking");

    companion object {
        val DEFAULT = FIXED_GUIDE

        fun fromId(id: String?): ScanMode = values().firstOrNull { it.id == id } ?: DEFAULT
    }
}

object ScanGuide {
    /** Guide side as a fraction of the view's shorter side. */
    const val SIDE_FRACTION = 0.6f

    /** Centred square framing guide for a [viewWidth]×[viewHeight] view, or null if empty. */
    fun guideBox(viewWidth: Int, viewHeight: Int, fraction: Float = SIDE_FRACTION): PreviewMapper.Box? {
        if (viewWidth <= 0 || viewHeight <= 0) return null
        val side = minOf(viewWidth, viewHeight) * fraction.coerceIn(0.1f, 1f)
        val cx = viewWidth / 2f
        val cy = viewHeight / 2f
        return PreviewMapper.Box(cx - side / 2f, cy - side / 2f, cx + side / 2f, cy + side / 2f)
    }

    /** True when the centre of [code] (view space) lies inside [guide]. */
    fun isInside(code: PreviewMapper.Box, guide: PreviewMapper.Box): Boolean =
        code.centerX >= guide.left && code.centerX <= guide.right &&
            code.centerY >= guide.top && code.centerY <= guide.bottom
}

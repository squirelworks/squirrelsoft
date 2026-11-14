package works.squirrel.qr

/**
 * Pure CameraX zoom / torch UI math. Kept free of Android Camera types so JVM
 * unit tests can cover clamping without a device. Real flash/torch and live
 * zoom still need a phone check.
 */
object CameraControls {

    /** SeekBar 0..100 ↔ CameraX linearZoom 0..1. */
    fun linearZoomToProgress(linearZoom: Float): Int =
        (linearZoom.coerceIn(0f, 1f) * 100f).toInt().coerceIn(0, 100)

    fun progressToLinearZoom(progress: Int): Float =
        progress.coerceIn(0, 100) / 100f

    /** Pinch scale applied to the current zoom ratio, clamped to the camera range. */
    fun nextZoomRatio(
        currentRatio: Float,
        scaleFactor: Float,
        minRatio: Float,
        maxRatio: Float,
    ): Float {
        val lo = minOf(minRatio, maxRatio)
        val hi = maxOf(minRatio, maxRatio)
        return (currentRatio * scaleFactor).coerceIn(lo, hi)
    }

    /**
     * Whether the torch toggle may be offered. [requestedOn] is ignored when
     * there is no flash unit — callers should force the control off then.
     */
    fun torchAvailable(hasFlashUnit: Boolean): Boolean = hasFlashUnit

    /** Effective torch state after a toggle attempt (no-op without flash). */
    fun nextTorchOn(hasFlashUnit: Boolean, currentlyOn: Boolean): Boolean {
        if (!hasFlashUnit) return false
        return !currentlyOn
    }
}

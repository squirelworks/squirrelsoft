package works.squirrel.qr

/**
 * Made codes as pixels. Pure Kotlin, so the JVM tests can check it; the
 * activity turns the result into a Bitmap.
 */
object QrRender {
    const val DARK = 0xFF000000.toInt()
    const val LIGHT = 0xFFFFFFFF.toInt()

    /** Quiet zone the standard asks for, in modules. */
    const val BORDER = 4

    /** The largest whole number of pixels per module that fits [targetPx], at least 1. */
    fun scaleFor(size: Int, targetPx: Int, border: Int = BORDER): Int =
        maxOf(1, targetPx / (size + 2 * border))

    /** Side of the image in pixels. */
    fun side(size: Int, scale: Int, border: Int = BORDER): Int = (size + 2 * border) * scale

    /** ARGB pixels, row by row, [side] x [side]: [scale] px per module and [border] light modules around. */
    fun pixels(symbol: SqQr.Symbol, scale: Int, border: Int = BORDER): IntArray =
        pixels(symbol.size, symbol.modules, scale, border)

    fun pixels(size: Int, modules: ByteArray, scale: Int, border: Int = BORDER): IntArray {
        require(scale >= 1 && border >= 0 && modules.size >= size * size)
        val side = side(size, scale, border)
        val out = IntArray(side * side) { LIGHT }
        for (r in 0 until size) {
            for (c in 0 until size) {
                if (modules[r * size + c].toInt() == 0) continue
                val y0 = (r + border) * scale
                val x0 = (c + border) * scale
                for (y in y0 until y0 + scale) {
                    out.fill(DARK, y * side + x0, y * side + x0 + scale)
                }
            }
        }
        return out
    }
}

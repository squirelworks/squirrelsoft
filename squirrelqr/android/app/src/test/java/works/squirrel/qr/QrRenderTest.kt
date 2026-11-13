package works.squirrel.qr

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class QrRenderTest {
    // A made-up 3x3 "symbol": a diagonal of dark modules.
    private val diag = byteArrayOf(1, 0, 0, 0, 1, 0, 0, 0, 1)

    @Test
    fun pixelsPlaceModulesInsideTheQuietZone() {
        val scale = 2
        val border = 1
        val side = QrRender.side(3, scale, border)
        assertEquals(10, side)
        val px = QrRender.pixels(3, diag, scale, border)
        assertEquals(side * side, px.size)
        fun at(x: Int, y: Int) = px[y * side + x]
        // border is light
        for (i in 0 until side) {
            assertEquals(QrRender.LIGHT, at(i, 0))
            assertEquals(QrRender.LIGHT, at(0, i))
            assertEquals(QrRender.LIGHT, at(i, side - 1))
        }
        // module (0,0) covers pixels 2..3 on both axes; (0,1) is light
        assertEquals(QrRender.DARK, at(2, 2))
        assertEquals(QrRender.DARK, at(3, 3))
        assertEquals(QrRender.LIGHT, at(4, 2))
        assertEquals(QrRender.DARK, at(7, 7))
        assertEquals(3 * scale * scale, px.count { it == QrRender.DARK })
    }

    @Test
    fun scaleFitsTheTargetAndNeverDropsBelowOne() {
        // 21 modules + 8 border = 29; 600 px / 29 = 20
        assertEquals(20, QrRender.scaleFor(21, 600))
        assertTrue(QrRender.side(21, QrRender.scaleFor(21, 600)) <= 600)
        assertEquals(1, QrRender.scaleFor(177, 50))
    }

    @Test(expected = IllegalArgumentException::class)
    fun tooFewModulesRejected() {
        QrRender.pixels(3, ByteArray(4), 1)
    }
}

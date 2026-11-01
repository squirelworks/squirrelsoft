package works.squirrel.qr

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class PreviewMapperTest {
    @Test
    fun mapsCenteredBoxWithFillCenter() {
        // Image 1000x1000 into a 500x1000 view → scale = max(0.5, 1) = 1
        // scaled 1000x1000; dx = -250; dy = 0
        val mapped = PreviewMapper.mapBox(
            left = 400,
            top = 400,
            right = 600,
            bottom = 600,
            imageWidth = 1000,
            imageHeight = 1000,
            viewWidth = 500,
            viewHeight = 1000
        )
        assertNotNull(mapped)
        assertEquals(150f, mapped!!.left, 0.5f)
        assertEquals(400f, mapped.top, 0.5f)
        assertEquals(350f, mapped.right, 0.5f)
        assertEquals(600f, mapped.bottom, 0.5f)
    }

    @Test
    fun rejectsZeroSizes() {
        assertNull(
            PreviewMapper.mapBox(0, 0, 10, 10, 0, 100, 200, 200)
        )
    }
}

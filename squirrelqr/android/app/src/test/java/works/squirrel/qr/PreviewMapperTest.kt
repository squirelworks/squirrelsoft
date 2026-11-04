package works.squirrel.qr

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
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

class PreviewMapperRotationTest {
    // Sensor buffer 640x480 (landscape). In portrait (rotation 90/270) ML Kit
    // returns boxes in the upright 480x640 frame.
    private val bufW = 640
    private val bufH = 480

    @Test
    fun uprightSizeSwapsForQuarterTurns() {
        assertEquals(640 to 480, PreviewMapper.uprightSize(bufW, bufH, 0))
        assertEquals(480 to 640, PreviewMapper.uprightSize(bufW, bufH, 90))
        assertEquals(640 to 480, PreviewMapper.uprightSize(bufW, bufH, 180))
        assertEquals(480 to 640, PreviewMapper.uprightSize(bufW, bufH, 270))
        assertEquals(480 to 640, PreviewMapper.uprightSize(bufW, bufH, -90))
    }

    @Test
    fun portraitCentredCodeLandsInViewCentre() {
        // Upright 480x640 image, code centred at (240, 320); portrait view 1080x1200.
        for (rotation in intArrayOf(90, 270)) {
            val b = PreviewMapper.mapBoxForBuffer(190, 270, 290, 370, bufW, bufH, rotation, 1080, 1200)!!
            assertEquals("rot $rotation", 540f, b.centerX, 0.5f)
            assertEquals("rot $rotation", 600f, b.centerY, 0.5f)
            // scale = max(1080/480, 1200/640) = 2.25 → 100 px box becomes 225 px.
            assertEquals(225f, b.right - b.left, 0.5f)
        }
    }

    @Test
    fun portraitWithoutRotationFixWasOffCentre() {
        // The old call passed the unrotated 640x480 size: the same code drifts.
        val wrong = PreviewMapper.mapBox(190, 270, 290, 370, bufW, bufH, 1080, 1200)!!
        assertTrue(kotlin.math.abs(wrong.centerX - 540f) > 50f || kotlin.math.abs(wrong.centerY - 600f) > 50f)
    }

    @Test
    fun landscapeCornersMapWithWideView() {
        // Landscape (0/180): upright 640x480 into a 1600x800 view → scale 2.5,
        // scaled 1600x1200, dy = -200.
        for (rotation in intArrayOf(0, 180)) {
            val b = PreviewMapper.mapBoxForBuffer(0, 80, 640, 400, bufW, bufH, rotation, 1600, 800)!!
            assertEquals(0f, b.left, 0.5f)
            assertEquals(1600f, b.right, 0.5f)
            assertEquals(0f, b.top, 0.5f)
            assertEquals(800f, b.bottom, 0.5f)
        }
    }
}

class ScanGuideTest {
    @Test
    fun guideIsCentredSquareOfShortSide() {
        val g = ScanGuide.guideBox(1000, 600)!!
        assertEquals(360f, g.right - g.left, 0.01f)
        assertEquals(360f, g.bottom - g.top, 0.01f)
        assertEquals(500f, g.centerX, 0.01f)
        assertEquals(300f, g.centerY, 0.01f)
        assertNull(ScanGuide.guideBox(0, 600))
    }

    @Test
    fun hitTestUsesCodeCentre() {
        val g = ScanGuide.guideBox(1000, 1000)!! // 200..800
        assertTrue(ScanGuide.isInside(PreviewMapper.Box(450f, 450f, 550f, 550f), g))
        // Box overlaps the guide but its centre is outside.
        assertFalse(ScanGuide.isInside(PreviewMapper.Box(780f, 400f, 900f, 500f), g))
        assertFalse(ScanGuide.isInside(PreviewMapper.Box(0f, 0f, 100f, 100f), g))
        // Edge counts as inside.
        assertTrue(ScanGuide.isInside(PreviewMapper.Box(150f, 450f, 250f, 550f), g))
    }

    @Test
    fun modeDefaultsToFixedAndRoundTrips() {
        assertEquals(ScanMode.FIXED_GUIDE, ScanMode.DEFAULT)
        assertEquals(ScanMode.FIXED_GUIDE, ScanMode.fromId(null))
        assertEquals(ScanMode.FIXED_GUIDE, ScanMode.fromId("bogus"))
        for (m in ScanMode.values()) assertEquals(m, ScanMode.fromId(m.id))
    }
}

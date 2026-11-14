package works.squirrel.qr

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CameraControlsTest {
    @Test
    fun linearZoomMapsToSeekBarProgress() {
        assertEquals(0, CameraControls.linearZoomToProgress(0f))
        assertEquals(100, CameraControls.linearZoomToProgress(1f))
        assertEquals(50, CameraControls.linearZoomToProgress(0.5f))
        assertEquals(0, CameraControls.linearZoomToProgress(-0.2f))
        assertEquals(100, CameraControls.linearZoomToProgress(1.5f))
        assertEquals(0f, CameraControls.progressToLinearZoom(0), 0f)
        assertEquals(1f, CameraControls.progressToLinearZoom(100), 0f)
        assertEquals(0.25f, CameraControls.progressToLinearZoom(25), 1e-6f)
        assertEquals(0f, CameraControls.progressToLinearZoom(-10), 0f)
        assertEquals(1f, CameraControls.progressToLinearZoom(999), 0f)
    }

    @Test
    fun pinchZoomClampsToCameraRange() {
        assertEquals(2f, CameraControls.nextZoomRatio(1f, 2f, 1f, 8f), 0f)
        assertEquals(8f, CameraControls.nextZoomRatio(5f, 3f, 1f, 8f), 0f)
        assertEquals(1f, CameraControls.nextZoomRatio(1.2f, 0.1f, 1f, 8f), 0f)
        assertEquals(4f, CameraControls.nextZoomRatio(2f, 2f, 8f, 1f), 0f) // swapped bounds
    }

    @Test
    fun torchToggleRequiresFlash() {
        assertFalse(CameraControls.torchAvailable(false))
        assertTrue(CameraControls.torchAvailable(true))
        assertFalse(CameraControls.nextTorchOn(hasFlashUnit = false, currentlyOn = false))
        assertFalse(CameraControls.nextTorchOn(hasFlashUnit = false, currentlyOn = true))
        assertTrue(CameraControls.nextTorchOn(hasFlashUnit = true, currentlyOn = false))
        assertFalse(CameraControls.nextTorchOn(hasFlashUnit = true, currentlyOn = true))
    }
}

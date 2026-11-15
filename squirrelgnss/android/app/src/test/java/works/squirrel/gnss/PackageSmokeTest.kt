package works.squirrel.gnss

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PackageSmokeTest {
    @Test
    fun gnssPackage() {
        assertEquals("works.squirrel.gnss", MainActivity::class.java.packageName)
        assertTrue(MainActivity::class.java.simpleName == "MainActivity")
    }
}

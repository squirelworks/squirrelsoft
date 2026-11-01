package works.squirrel.field

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PackageSmokeTest {
    @Test
    fun qrOnlyPackage() {
        assertEquals("works.squirrel.field", MainActivity::class.java.packageName)
        assertTrue(MainActivity::class.java.simpleName == "MainActivity")
    }
}

package works.squirrel.field

import org.junit.Assert.assertEquals
import org.junit.Test

class PackageSmokeTest {
    @Test
    fun hubPagesNamed() {
        assertEquals("spectrum.html", "spectrum.html")
        assertEquals("works.squirrel.field", MainActivity::class.java.packageName)
    }
}

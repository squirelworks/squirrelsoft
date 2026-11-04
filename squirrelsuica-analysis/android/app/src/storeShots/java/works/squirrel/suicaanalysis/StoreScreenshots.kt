package works.squirrel.suicaanalysis

import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Looper
import android.view.View
import android.widget.ScrollView
import android.widget.TextView
import com.google.android.material.button.MaterialButton
import com.google.android.material.tabs.TabLayout
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File

/**
 * Renders the real MainActivity layout for Play Store phone screenshots.
 *
 * Opt-in only: `./gradlew :app:testDebugUnitTest -PstoreShots --tests '*StoreScreenshots*'`.
 * The card is synthetic ([StoreCard]); no real card data is used. PNGs go to
 * the directory in the `storeShots.out` system property (default build/store-shots).
 * 360x640 dp at xxhdpi = 1080x1920 px, a 9:16 phone screenshot.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34], qualifiers = "w360dp-h640dp-xxhdpi")
class StoreScreenshots {
    private val out = File(System.getProperty("storeShots.out") ?: "build/store-shots").apply { mkdirs() }

    private fun scanned(): MainActivity {
        val activity = Robolectric.buildActivity(MainActivity::class.java).setup().get()
        // A fixed fake clock gives stable, phone-like latencies (about 3-6 ms per command).
        var t = 0L
        var step = 0
        val clock = { t += (if (step++ % 2 == 0) 0L else 2_600_000L + (step * 997_003L) % 3_100_000L); t }
        val dump = CardAnalyzer(StoreCard(), clock, maxBlocks = 20)
            .analyze(StoreCard.IDM_SYS0, "2026-11-04T09:12:44Z", "SquirrelSuicaAnalysis android")
        MainActivity::class.java.getDeclaredField("dump").apply { isAccessible = true }.set(activity, dump)
        activity.findViewById<MaterialButton>(R.id.btnShare).isEnabled = true
        activity.findViewById<MaterialButton>(R.id.btnSave).isEnabled = true
        activity.findViewById<TextView>(R.id.status).text = "Done: ${dump.log.size} commands"
        MainActivity::class.java.getDeclaredMethod("render").apply { isAccessible = true }.invoke(activity)
        idle()
        return activity
    }

    private fun idle() = shadowOf(Looper.getMainLooper()).idle()

    private fun shoot(activity: MainActivity, name: String) {
        idle()
        val root: View = activity.window.decorView
        val w = 1080
        val h = 1920
        root.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY))
        root.layout(0, 0, w, h)
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        root.draw(Canvas(bmp))
        File(out, "$name.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun summary() = shoot(scanned(), "01-summary")

    @Test
    fun blocks() {
        val a = scanned()
        val out = a.findViewById<TextView>(R.id.output)
        val scroll = a.findViewById<ScrollView>(R.id.scroll)
        shoot(a, "tmp") // lay out once so line positions are known
        val line = out.text.lines().indexOfFirst { it.contains("svc 090f") }
        scroll.scrollTo(0, out.paddingTop + out.layout.getLineTop(line.coerceAtLeast(0)))
        shoot(a, "02-history-blocks")
        File(this.out, "tmp.png").delete()
    }

    @Test
    fun log() {
        val a = scanned()
        a.findViewById<TabLayout>(R.id.tabs).getTabAt(1)!!.select()
        shoot(a, "03-command-log")
    }
}

/**
 * Synthetic Suica-like card for screenshots: system 0003 with transit-style
 * services (some readable without a key, some not) and a common-area system FE00.
 * Values are made up; they only follow the shape of FeliCa replies.
 */
class StoreCard : Transceiver {
    companion object {
        val IDM_SYS0 = byteArrayOf(0x01, 0x01, 0x12, 0x34, 0x56, 0x78, 0x9a.toByte(), 0xbc.toByte())
        val IDM_SYS1 = byteArrayOf(0x11, 0x01, 0x12, 0x34, 0x56, 0x78, 0x9a.toByte(), 0xbc.toByte())
        val PMM = byteArrayOf(0x10, 0x0b, 0x4b, 0x42, 0x84.toByte(), 0x85.toByte(), 0x10, 0xff.toByte())
    }

    private var idm = IDM_SYS0
    private val nodes = mapOf(
        0x0003 to listOf(
            intArrayOf(0x0000, 0xfffe), intArrayOf(0x0040, 0x07ff), intArrayOf(0x008b),
            intArrayOf(0x0800, 0x0fff), intArrayOf(0x090f), intArrayOf(0x1000, 0x17ff),
            intArrayOf(0x108f), intArrayOf(0x10c8), intArrayOf(0x184a)
        ),
        0xfe00 to listOf(intArrayOf(0x0000, 0xfffe), intArrayOf(0x1a80, 0x1abf), intArrayOf(0x1a88))
    )
    private var system = 0x0003
    private val sizes = mapOf(0x090f to 20, 0x008b to 1, 0x108f to 3)

    private fun u(b: ByteArray) = b.map { it.toInt() and 0xff }

    private fun reply(code: Int, data: List<Int>): ByteArray {
        val b = byteArrayOf(0, code.toByte()) + idm + data.map { it.toByte() }.toByteArray()
        b[0] = b.size.toByte()
        return b
    }

    private fun block(svc: Int, i: Int): List<Int> = when (svc) {
        0x008b -> listOf(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0xd8, 0x0b, 0x00, 0x00, 0x42)
        0x108f -> listOf(0xa0, 0x02, 0x00, 0x00, 0xe1, 0x23, 0x41, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, i)
        else -> {
            // History-shaped row: kind, process, date, in/out line+station, balance, sequence.
            val day = 0x5a00 + ((30 - i) and 0x1f)
            val bal = 4980 - i * 178
            val seq = 0x0290 - i
            listOf(
                0x16, if (i % 5 == 3) 0x02 else 0x01, 0x00, 0x02, day shr 8, day and 0xff,
                0xe1, 0x10 + i * 3 and 0xff, 0xe2, 0x20 + i * 5 and 0xff,
                bal and 0xff, (bal shr 8) and 0xff, 0x00, seq shr 8, seq and 0xff, 0x00
            )
        }
    }

    override fun transceive(command: ByteArray): ByteArray {
        val c = u(command)
        return when (c[1]) {
            0x00 -> {
                val sc = (c[2] shl 8) or c[3]
                val target = when (sc) {
                    0xffff, 0x0003 -> 0x0003
                    0xfe00 -> 0xfe00
                    else -> return byteArrayOf()
                }
                system = target
                idm = if (target == 0xfe00) IDM_SYS1 else IDM_SYS0
                val rd = when (c[4]) {
                    1 -> listOf(target shr 8, target and 0xff)
                    2 -> listOf(0x00, 0x83)
                    else -> emptyList()
                }
                val b = byteArrayOf(0, 0x01) + idm + PMM + rd.map { it.toByte() }.toByteArray()
                b[0] = b.size.toByte()
                b
            }
            0x0c -> reply(0x0d, listOf(2, 0x00, 0x03, 0xfe, 0x00))
            0x04 -> reply(0x05, listOf(0))
            0x0a -> {
                val idx = c[10] or (c[11] shl 8)
                val node = nodes[system]?.getOrNull(idx) ?: return reply(0x0b, listOf(0xff, 0xff))
                reply(0x0b, node.flatMap { listOf(it and 0xff, it shr 8) })
            }
            0x02 -> {
                val n = c[10]
                reply(0x03, listOf(n) + (0 until n).flatMap { listOf(0x00, 0x00) })
            }
            0x06 -> {
                val svc = c[11] or (c[12] shl 8)
                val n = c[13]
                val blocks = ArrayList<Int>()
                var off = 14
                repeat(n) {
                    if (c[off] and 0x80 != 0) {
                        blocks.add(c[off + 1]); off += 2
                    } else {
                        blocks.add(c[off + 1] or (c[off + 2] shl 8)); off += 3
                    }
                }
                val size = sizes[svc] ?: return reply(0x07, listOf(0x01, 0xa6))
                if (n > 4) return reply(0x07, listOf(0x01, 0xa2))
                if (blocks.any { it >= size }) return reply(0x07, listOf(0x01, 0xa8))
                reply(0x07, listOf(0, 0, n) + blocks.flatMap { block(svc, it) })
            }
            else -> byteArrayOf()
        }
    }
}

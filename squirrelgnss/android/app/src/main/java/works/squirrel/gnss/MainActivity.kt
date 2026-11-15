package works.squirrel.gnss

import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding

/**
 * First Android shell for SquirrelGNSS: runs the synthetic-sky demo through
 * JNI. No location permission and no phone GNSS hardware yet.
 */
class MainActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        setContentView(R.layout.activity_main)

        val root = findViewById<android.view.View>(R.id.root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.updatePadding(bars.left, bars.top, bars.right, bars.bottom)
            insets
        }

        val status = findViewById<TextView>(R.id.status)
        val run = findViewById<Button>(R.id.run_demo)

        fun refresh() {
            if (!SqGnss.available) {
                status.text = getString(R.string.status_no_core)
                return
            }
            val truth = SqGnss.Llh(35.681236, 139.767125, 40.0)
            val fix = SqGnss.demoSolve(truth) ?: run {
                status.text = getString(R.string.status_solve_failed)
                return
            }
            val delta = SqGnss.stakeoutDelta(truth, SqGnss.Llh(35.682236, 139.767125, 40.0))
            status.text = getString(
                R.string.status_demo,
                fix.llh.latDeg,
                fix.llh.lonDeg,
                fix.llh.heightM,
                fix.nUsed,
                fix.hdop,
                fix.vdop,
                fix.errM,
                delta?.n ?: Double.NaN,
            )
        }

        run.setOnClickListener { refresh() }
        refresh()
    }
}

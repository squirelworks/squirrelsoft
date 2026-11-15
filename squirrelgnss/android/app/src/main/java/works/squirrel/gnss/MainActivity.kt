package works.squirrel.gnss

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding

/**
 * SquirrelGNSS on Android: synthetic measurement feed through JNI by default
 * (CI / no hardware). Optionally listens for phone GnssMeasurement / Location
 * when the user grants location permission and a GPS provider exists.
 */
class MainActivity : AppCompatActivity(), PhoneGnssFeed.Listener {
    private lateinit var status: TextView
    private var phoneFeed: PhoneGnssFeed? = null
    private var lastPhoneLine: String = ""

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startPhoneFeed() else appendStatus(getString(R.string.status_permission_denied))
        }

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

        status = findViewById(R.id.status)
        val runSynthetic = findViewById<Button>(R.id.run_demo)
        val runPhone = findViewById<Button>(R.id.run_phone)

        runSynthetic.setOnClickListener { refreshSynthetic() }
        runPhone.setOnClickListener { requestPhoneOrStart() }
        refreshSynthetic()
    }

    override fun onStop() {
        phoneFeed?.stop()
        phoneFeed = null
        super.onStop()
    }

    private fun refreshSynthetic() {
        if (!SqGnss.available) {
            status.text = getString(R.string.status_no_core)
            return
        }
        val result = SyntheticMeasFeed.run() ?: run {
            status.text = getString(R.string.status_solve_failed)
            return
        }
        val fix = result.fix
        val delta = SqGnss.stakeoutDelta(
            result.truth,
            SqGnss.Llh(result.truth.latDeg + 0.001, result.truth.lonDeg, result.truth.heightM),
        )
        status.text = getString(
            R.string.status_synthetic,
            result.epoch.observations.size,
            fix.llh.latDeg,
            fix.llh.lonDeg,
            fix.llh.heightM,
            fix.nUsed,
            fix.hdop,
            fix.vdop,
            fix.errM,
            delta?.n ?: Double.NaN,
            lastPhoneLine.ifEmpty { getString(R.string.status_phone_idle) },
        )
    }

    private fun requestPhoneOrStart() {
        when {
            ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) ==
                PackageManager.PERMISSION_GRANTED -> startPhoneFeed()
            else -> permissionLauncher.launch(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    private fun startPhoneFeed() {
        if (phoneFeed == null) {
            phoneFeed = PhoneGnssFeed(this, this)
        }
        val feed = phoneFeed ?: return
        if (!feed.hasGpsProvider()) {
            appendStatus(getString(R.string.status_no_gps))
            return
        }
        if (!feed.start()) {
            appendStatus(getString(R.string.status_phone_start_failed))
        }
    }

    override fun onMeasurementEpoch(epoch: MeasurementEpoch, fix: SqGnss.Fix?) {
        lastPhoneLine = if (fix != null) {
            getString(
                R.string.status_phone_meas,
                epoch.observations.size,
                fix.llh.latDeg,
                fix.llh.lonDeg,
                fix.nUsed,
            )
        } else {
            getString(R.string.status_phone_meas_no_fix, epoch.observations.size)
        }
        refreshSynthetic()
    }

    override fun onLocationFix(llh: SqGnss.Llh, accuracyM: Float, provider: String) {
        lastPhoneLine = getString(R.string.status_phone_location, provider, llh.latDeg, llh.lonDeg, accuracyM)
        // Location feed still uses the geodesy JNI path (stakeout / ECEF).
        if (SqGnss.available) {
            SqGnss.llhToEcef(llh)
        }
        refreshSynthetic()
    }

    override fun onStatus(message: String) {
        appendStatus(message)
    }

    private fun appendStatus(message: String) {
        lastPhoneLine = message
        if (::status.isInitialized) refreshSynthetic()
    }
}

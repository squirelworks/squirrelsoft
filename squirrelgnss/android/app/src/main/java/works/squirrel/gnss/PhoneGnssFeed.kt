package works.squirrel.gnss

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.GnssClock
import android.location.GnssMeasurement
import android.location.GnssMeasurementsEvent
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat

/**
 * Optional phone GNSS / Location feed into the same packed observation path
 * as [SyntheticMeasFeed]. When raw [GnssMeasurement]s are available and the
 * clock bias is known, rows are mapped through [GnssRawMapper] and solved
 * with demo ephemerides (a first wire-up; broadcast eph from the phone /
 * NTRIP come later). When raw measurements are missing, [Location] updates
 * are reported as a coarse fix without observation PVT.
 *
 * CI and emulators without GNSS hardware never start this feed; they use
 * the synthetic path instead.
 */
class PhoneGnssFeed(
    private val context: Context,
    private val listener: Listener,
    private val mainHandler: Handler = Handler(Looper.getMainLooper()),
) {
    interface Listener {
        /** Packed raw measurements → optional PVT (may be null if solve fails). */
        fun onMeasurementEpoch(epoch: MeasurementEpoch, fix: SqGnss.Fix?)

        /** Coarse Location when raw GNSS is unavailable or as a companion. */
        fun onLocationFix(llh: SqGnss.Llh, accuracyM: Float, provider: String)

        fun onStatus(message: String)
    }

    private val locationManager =
        context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager

    @Volatile
    private var started = false

    private val locationListener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            listener.onLocationFix(
                SqGnss.Llh(location.latitude, location.longitude, location.altitude),
                location.accuracy,
                location.provider ?: "location",
            )
        }

        @Deprecated("Deprecated in API")
        override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) = Unit

        override fun onProviderEnabled(provider: String) = Unit
        override fun onProviderDisabled(provider: String) = Unit
    }

    private val measurementsCallback =
        object : GnssMeasurementsEvent.Callback() {
            override fun onGnssMeasurementsReceived(event: GnssMeasurementsEvent) {
                val epoch = mapEvent(event) ?: return
                val fix = if (SqGnss.available) SqGnss.solveEpoch(epoch) else null
                mainHandler.post { listener.onMeasurementEpoch(epoch, fix) }
            }

            override fun onStatusChanged(status: Int) {
                mainHandler.post {
                    listener.onStatus("GnssMeasurements status=$status")
                }
            }
        }

    fun hasLocationPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    /** True when the platform reports a GPS provider (hardware may still be cold). */
    fun hasGpsProvider(): Boolean =
        locationManager?.allProviders?.contains(LocationManager.GPS_PROVIDER) == true

    @SuppressLint("MissingPermission")
    fun start(): Boolean {
        if (started) return true
        if (!hasLocationPermission()) {
            listener.onStatus("Location permission not granted")
            return false
        }
        val lm = locationManager
        if (lm == null) {
            listener.onStatus("No LocationManager")
            return false
        }
        started = true
        try {
            lm.requestLocationUpdates(
                LocationManager.GPS_PROVIDER,
                1000L,
                0f,
                locationListener,
                Looper.getMainLooper(),
            )
        } catch (e: SecurityException) {
            started = false
            listener.onStatus("Location denied: ${e.message}")
            return false
        } catch (e: IllegalArgumentException) {
            listener.onStatus("GPS provider missing: ${e.message}")
        }
        val registered = try {
            if (Build.VERSION.SDK_INT >= 30) {
                lm.registerGnssMeasurementsCallback(measurementsCallback, mainHandler)
            } else {
                @Suppress("DEPRECATION")
                lm.registerGnssMeasurementsCallback(measurementsCallback)
            }
        } catch (e: Exception) {
            listener.onStatus("GnssMeasurements unavailable: ${e.message}")
            false
        }
        listener.onStatus(
            if (registered) "Phone GNSS + Location listening"
            else "Location listening (no raw GnssMeasurements)",
        )
        return true
    }

    fun stop() {
        if (!started) return
        started = false
        try {
            locationManager?.removeUpdates(locationListener)
        } catch (_: Exception) {
        }
        try {
            locationManager?.unregisterGnssMeasurementsCallback(measurementsCallback)
        } catch (_: Exception) {
        }
    }

    companion object {
        /** Map a platform event to a packed epoch (GPS rows only). */
        fun mapEvent(event: GnssMeasurementsEvent, tReceiver: Double = towFromClock(event.clock)): MeasurementEpoch? {
            val clock = event.clock
            // GnssMeasurementsEvent.measurements is a Collection, not a List.
            val meas = event.measurements.toList()
            if (meas.isEmpty()) return null
            val constellation = IntArray(meas.size)
            val svid = IntArray(meas.size)
            val cn0 = DoubleArray(meas.size)
            val rxSv = LongArray(meas.size)
            val stateOk = BooleanArray(meas.size)
            for (i in meas.indices) {
                val m = meas[i]
                constellation[i] = m.constellationType
                svid[i] = m.svid
                cn0[i] = m.cn0DbHz
                rxSv[i] = m.receivedSvTimeNanos
                stateOk[i] = measurementStateOk(m)
            }
            return GnssRawMapper.epochFromRaw(
                tReceiver = tReceiver,
                timeNanos = clock.timeNanos,
                fullBiasNanos = if (clock.hasFullBiasNanos()) clock.fullBiasNanos else 0L,
                biasNanos = if (clock.hasBiasNanos()) clock.biasNanos else 0.0,
                hasFullBias = clock.hasFullBiasNanos(),
                hasBias = clock.hasBiasNanos(),
                constellation = constellation,
                svid = svid,
                cn0 = cn0,
                receivedSvTimeNanos = rxSv,
                stateOk = stateOk,
                source = MeasurementEpoch.Source.PHONE_GNSS,
                tEph = tReceiver,
            )
        }

        private fun towFromClock(clock: GnssClock): Double {
            val tow = GnssRawMapper.receiverTimeOfWeekSec(
                clock.timeNanos,
                if (clock.hasFullBiasNanos()) clock.fullBiasNanos else 0L,
                if (clock.hasBiasNanos()) clock.biasNanos else 0.0,
                clock.hasFullBiasNanos(),
                clock.hasBiasNanos(),
            )
            return tow ?: 100000.0
        }

        private fun measurementStateOk(m: GnssMeasurement): Boolean {
            val state = m.state
            val codeLock = state and GnssMeasurement.STATE_CODE_LOCK != 0
            val todDecoded = if (Build.VERSION.SDK_INT >= 29) {
                state and GnssMeasurement.STATE_TOW_DECODED != 0 ||
                    state and GnssMeasurement.STATE_TOW_KNOWN != 0
            } else {
                state and GnssMeasurement.STATE_TOW_DECODED != 0
            }
            return codeLock && todDecoded
        }
    }
}

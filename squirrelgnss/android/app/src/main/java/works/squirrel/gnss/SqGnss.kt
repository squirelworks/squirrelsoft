package works.squirrel.gnss

/**
 * Kotlin side of the JNI bridge to the desktop SquirrelGNSS core (`sqgnss.c`).
 *
 * First step (#146): geodesy, stakeout, synthetic-sky [demoSolve].
 * Second step: packed observation epochs via [fillDemoEpoch] / [solveEpoch]
 * so a measurement feed (synthetic for CI, phone GnssMeasurement or Location
 * when hardware is present) can drive the same PVT path. NTRIP corrections
 * stay open.
 *
 * Packed solve output [OUT_LEN] matches `sqgnss_jni.c`; keep both in step.
 */
object SqGnss {
    const val OUT_LEN = 9
    const val MAX_SATS = 32

    /** False when the native library is missing (e.g. JVM unit tests). */
    val available: Boolean by lazy {
        try {
            System.loadLibrary("sqgnss_jni")
            true
        } catch (_: UnsatisfiedLinkError) {
            false
        }
    }

    data class Llh(val latDeg: Double, val lonDeg: Double, val heightM: Double)

    data class Ecef(val x: Double, val y: Double, val z: Double)

    data class Enu(val e: Double, val n: Double, val u: Double)

    /** One PVT fix from the native core (demo or fed epoch). */
    data class Fix(
        val llh: Llh,
        val clockBiasM: Double,
        val nUsed: Int,
        val hdop: Double,
        val vdop: Double,
        val rmsM: Double,
        /** 3-D error vs truth when truth was supplied; else negative. */
        val errM: Double,
    )

    /** @deprecated Use [Fix]; kept as a type alias for the first-step API. */
    typealias DemoFix = Fix

    @JvmStatic
    private external fun nativeLlhToEcef(llh: DoubleArray, ecef: DoubleArray): Boolean

    @JvmStatic
    private external fun nativeEcefToLlh(ecef: DoubleArray, llh: DoubleArray): Boolean

    @JvmStatic
    private external fun nativeStakeoutDelta(fromLlh: DoubleArray, toLlh: DoubleArray, enu: DoubleArray): Boolean

    @JvmStatic
    private external fun nativeDemoSolve(
        lat: Double,
        lon: Double,
        heightM: Double,
        tGps: Double,
        clockBiasM: Double,
        noiseM: Double,
        seed: Int,
        out: DoubleArray,
    ): Boolean

    @JvmStatic
    private external fun nativeFillDemoEpoch(
        lat: Double,
        lon: Double,
        heightM: Double,
        tGps: Double,
        clockBiasM: Double,
        noiseM: Double,
        seed: Int,
        tReceiverOut: DoubleArray,
        prns: IntArray,
        ranges: DoubleArray,
        snrs: DoubleArray,
    ): Int

    @JvmStatic
    private external fun nativeSolveEpoch(
        tReceiver: Double,
        tEph: Double,
        prns: IntArray,
        ranges: DoubleArray,
        snrs: DoubleArray,
        n: Int,
        truthLlh: DoubleArray?,
        out: DoubleArray,
    ): Boolean

    fun llhToEcef(llh: Llh): Ecef? {
        if (!available) return null
        val xyz = DoubleArray(3)
        if (!nativeLlhToEcef(doubleArrayOf(llh.latDeg, llh.lonDeg, llh.heightM), xyz)) return null
        return Ecef(xyz[0], xyz[1], xyz[2])
    }

    fun ecefToLlh(ecef: Ecef): Llh? {
        if (!available) return null
        val g = DoubleArray(3)
        if (!nativeEcefToLlh(doubleArrayOf(ecef.x, ecef.y, ecef.z), g)) return null
        return Llh(g[0], g[1], g[2])
    }

    fun stakeoutDelta(from: Llh, to: Llh): Enu? {
        if (!available) return null
        val enu = DoubleArray(3)
        if (!nativeStakeoutDelta(
                doubleArrayOf(from.latDeg, from.lonDeg, from.heightM),
                doubleArrayOf(to.latDeg, to.lonDeg, to.heightM),
                enu,
            )
        ) {
            return null
        }
        return Enu(enu[0], enu[1], enu[2])
    }

    /**
     * Solve a synthetic epoch at [truth] (no phone GNSS). [noiseM] 0 is
     * noiseless millimetre-class; seeded noise matches the desktop demo.
     */
    fun demoSolve(
        truth: Llh,
        tGps: Double = 100000.0,
        clockBiasM: Double = 15.0,
        noiseM: Double = 0.0,
        seed: Int = 1,
    ): Fix? {
        if (!available) return null
        val out = DoubleArray(OUT_LEN)
        if (!nativeDemoSolve(truth.latDeg, truth.lonDeg, truth.heightM, tGps, clockBiasM, noiseM, seed, out)) {
            return null
        }
        return fixFromOut(out)
    }

    /**
     * Fill packed observation arrays from the synthetic sky at [truth].
     * Returns the epoch, or null when the native core is missing / failed.
     */
    fun fillDemoEpoch(
        truth: Llh,
        tGps: Double = 100000.0,
        clockBiasM: Double = 15.0,
        noiseM: Double = 0.0,
        seed: Int = 1,
    ): MeasurementEpoch? {
        if (!available) return null
        val tReceiverOut = DoubleArray(1)
        val prns = IntArray(MAX_SATS)
        val ranges = DoubleArray(MAX_SATS)
        val snrs = DoubleArray(MAX_SATS)
        val n = nativeFillDemoEpoch(
            truth.latDeg, truth.lonDeg, truth.heightM, tGps, clockBiasM, noiseM, seed,
            tReceiverOut, prns, ranges, snrs,
        )
        if (n < 4) return null
        // t_receiver = t_true + clock/c; keep t_true as tEph for demo orbits.
        return MeasurementEpoch.fromPacked(
            tReceiver = tReceiverOut[0],
            prns = prns,
            ranges = ranges,
            snrs = snrs,
            n = n,
            tEph = tGps,
        )
    }

    /**
     * Solve PVT from a packed observation epoch against demo ephemerides at
     * [MeasurementEpoch.tEph]. When [truth] is set, [Fix.errM] is the 3-D
     * error in metres.
     */
    fun solveEpoch(epoch: MeasurementEpoch, truth: Llh? = null): Fix? {
        if (!available) return null
        if (epoch.observations.size < 4) return null
        val packed = epoch.pack(MAX_SATS) ?: return null
        val out = DoubleArray(OUT_LEN)
        val truthArr = truth?.let { doubleArrayOf(it.latDeg, it.lonDeg, it.heightM) }
        if (!nativeSolveEpoch(
                epoch.tReceiver, epoch.tEph, packed.prns, packed.ranges, packed.snrs, packed.n, truthArr, out,
            )
        ) {
            return null
        }
        return fixFromOut(out)
    }

    private fun fixFromOut(out: DoubleArray): Fix =
        Fix(
            llh = Llh(out[0], out[1], out[2]),
            clockBiasM = out[3],
            nUsed = out[4].toInt(),
            hdop = out[5],
            vdop = out[6],
            rmsM = out[7],
            errM = out[8],
        )
}

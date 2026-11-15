package works.squirrel.gnss

/**
 * Kotlin side of the JNI bridge to the desktop SquirrelGNSS core (`sqgnss.c`),
 * the same geodesy and synthetic-sky PVT the CLI uses. Phone
 * `GnssMeasurement` / NTRIP are not wired yet — this is the first Android
 * step so CI can exercise the bridge with synthetic data only.
 *
 * [nativeDemoSolve] fills [out] (length >= [OUT_LEN]) as documented in
 * `sqgnss_jni.c`; keep both in step.
 */
object SqGnss {
    const val OUT_LEN = 9

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

    /** One synthetic-sky fix: solution LLH, quality, and 3-D error vs the truth site. */
    data class DemoFix(
        val llh: Llh,
        val clockBiasM: Double,
        val nUsed: Int,
        val hdop: Double,
        val vdop: Double,
        val rmsM: Double,
        val errM: Double,
    )

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
    ): DemoFix? {
        if (!available) return null
        val out = DoubleArray(OUT_LEN)
        if (!nativeDemoSolve(truth.latDeg, truth.lonDeg, truth.heightM, tGps, clockBiasM, noiseM, seed, out)) {
            return null
        }
        return DemoFix(
            llh = Llh(out[0], out[1], out[2]),
            clockBiasM = out[3],
            nUsed = out[4].toInt(),
            hdop = out[5],
            vdop = out[6],
            rmsM = out[7],
            errM = out[8],
        )
    }
}

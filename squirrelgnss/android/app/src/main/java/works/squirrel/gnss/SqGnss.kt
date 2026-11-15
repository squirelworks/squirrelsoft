package works.squirrel.gnss

/**
 * Kotlin side of the JNI bridge to the desktop SquirrelGNSS cores
 * (`sqgnss.c`, `sqntrip`, `sqrtcm`).
 *
 * First step (#146): geodesy, stakeout, synthetic-sky [demoSolve].
 * Second step: packed observation epochs via [fillDemoEpoch] / [solveEpoch]
 * so a measurement feed (synthetic for CI, phone GnssMeasurement or Location
 * when hardware is present) can drive the same PVT path.
 * Third step: NTRIP caster wiring — [ntripSynthMsm] / [ntripLoopbackMsm]
 * (CI-safe) and [ntripDial] (operator TCP; never from CI).
 *
 * Packed solve output [OUT_LEN] and NTRIP summary [NTRIP_OUT_LEN] match
 * `sqgnss_jni.c`; keep both in step.
 */
object SqGnss {
    const val OUT_LEN = 9
    const val NTRIP_OUT_LEN = 12
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

    /**
     * Summary of one NTRIP stream 200 body after RTCM framing + MSM parse.
     * Layout matches `nativeNtrip*` / `NTRIP_OUT_LEN` in `sqgnss_jni.c`.
     */
    data class NtripMsmResult(
        val status: Int,
        val kind: Int,
        val bodyLen: Int,
        val nFrames: Int,
        val msgType: Int,
        val stationId: Int,
        val epochMs: Int,
        val msmType: Int,
        val nSat: Int,
        val nSig: Int,
        val nCell: Int,
        val category: Int,
    ) {
        val isMsmStream: Boolean
            get() = status == 200 && kind == 1 && nFrames >= 1 && msmType in 1..7
    }

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

    @JvmStatic
    private external fun nativeNtripSynthMsm(out: IntArray): Boolean

    @JvmStatic
    private external fun nativeNtripFeedResponse(bytes: ByteArray, out: IntArray): Boolean

    @JvmStatic
    private external fun nativeNtripDial(
        host: String,
        port: String,
        mount: String,
        user: String,
        pass: String,
        ntripVersion: Int,
        out: IntArray,
    ): Boolean

    @JvmStatic
    private external fun nativeNtripLoopbackMsm(out: IntArray): Boolean

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

    /**
     * Offline memio NTRIP: synthetic ICY 200 + MSM4 obs → framed MSM summary.
     * CI / host smoke only — no sockets.
     */
    fun ntripSynthMsm(): NtripMsmResult? {
        if (!available) return null
        val out = IntArray(NTRIP_OUT_LEN)
        if (!nativeNtripSynthMsm(out)) return null
        return ntripFromOut(out)
    }

    /**
     * Feed a full caster reply (headers + body) through `sqntrip` → `sqrtcm`.
     * Useful when the host already holds synthetic bytes (never real dumps).
     */
    fun ntripFeedResponse(bytes: ByteArray): NtripMsmResult? {
        if (!available) return null
        val out = IntArray(NTRIP_OUT_LEN)
        if (!nativeNtripFeedResponse(bytes, out)) return null
        return ntripFromOut(out)
    }

    /**
     * Live TCP dial to [host]:[port] for [mount]. Operator-only; CI must not
     * call this. Empty [user]/[pass] skips Basic auth.
     */
    fun ntripDial(
        host: String,
        port: String,
        mount: String,
        user: String = "",
        pass: String = "",
        ntripVersion: Int = 1,
    ): NtripMsmResult? {
        if (!available) return null
        if (host.isBlank() || port.isBlank() || mount.isBlank()) return null
        val out = IntArray(NTRIP_OUT_LEN)
        if (!nativeNtripDial(host, port, mount, user, pass, ntripVersion, out)) return null
        return ntripFromOut(out)
    }

    /**
     * `127.0.0.1` loopback synthetic caster (TCP). Proves Android sockets +
     * MSM parse without a remote host.
     */
    fun ntripLoopbackMsm(): NtripMsmResult? {
        if (!available) return null
        val out = IntArray(NTRIP_OUT_LEN)
        if (!nativeNtripLoopbackMsm(out)) return null
        return ntripFromOut(out)
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

    private fun ntripFromOut(out: IntArray): NtripMsmResult =
        NtripMsmResult(
            status = out[0],
            kind = out[1],
            bodyLen = out[2],
            nFrames = out[3],
            msgType = out[4],
            stationId = out[5],
            epochMs = out[6],
            msmType = out[7],
            nSat = out[8],
            nSig = out[9],
            nCell = out[10],
            category = out[11],
        )
}

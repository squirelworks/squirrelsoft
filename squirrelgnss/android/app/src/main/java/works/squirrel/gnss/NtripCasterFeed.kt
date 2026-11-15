package works.squirrel.gnss

/**
 * High-level NTRIP → RTCM MSM path for the Android UI.
 *
 * CI and the host JNI smoke use [runSynth] / [runLoopback] only (memio or
 * `127.0.0.1`). [dial] is for an operator-chosen caster; never call it from
 * tests or CI, and never embed real credentials in the tree.
 */
object NtripCasterFeed {
    /**
     * Offline memio: synthetic ICY 200 + MSM4 → [SqGnss.NtripMsmResult].
     * Preferred CI / unit path.
     */
    fun runSynth(): SqGnss.NtripMsmResult? = SqGnss.ntripSynthMsm()

    /**
     * Local loopback TCP synthetic caster (`127.0.0.1`). Proves the same
     * sockets path Android would use for a live dial, without leaving the
     * machine.
     */
    fun runLoopback(): SqGnss.NtripMsmResult? = SqGnss.ntripLoopbackMsm()

    /**
     * Live TCP dial. Operator-only; not invoked from CI.
     */
    fun dial(
        host: String,
        port: String,
        mount: String,
        user: String = "",
        pass: String = "",
        ntripVersion: Int = 1,
    ): SqGnss.NtripMsmResult? =
        SqGnss.ntripDial(host, port, mount, user, pass, ntripVersion)
}

package works.squirrel.gnss

/**
 * CI / demo measurement source: builds a synthetic observation epoch through
 * the JNI core ([SqGnss.fillDemoEpoch]) and solves it with [SqGnss.solveEpoch].
 * No phone GNSS hardware. Matches the desktop synthetic sky.
 */
object SyntheticMeasFeed {
    val defaultTruth = SqGnss.Llh(35.681236, 139.767125, 40.0)

    data class Result(
        val epoch: MeasurementEpoch,
        val fix: SqGnss.Fix,
        val truth: SqGnss.Llh,
    )

    /**
     * Fill a synthetic epoch at [truth] and solve it. Returns null when the
     * native core is missing or the solve fails.
     */
    fun run(
        truth: SqGnss.Llh = defaultTruth,
        tGps: Double = 100000.0,
        clockBiasM: Double = 15.0,
        noiseM: Double = 0.0,
        seed: Int = 1,
    ): Result? {
        val epoch = SqGnss.fillDemoEpoch(truth, tGps, clockBiasM, noiseM, seed) ?: return null
        val fix = SqGnss.solveEpoch(epoch, truth) ?: return null
        return Result(epoch, fix, truth)
    }
}

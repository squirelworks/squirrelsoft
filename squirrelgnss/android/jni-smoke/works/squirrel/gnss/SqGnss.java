package works.squirrel.gnss;

/*
 * Host smoke test for the Android JNI bridge (app/src/main/cpp/sqgnss_jni.c).
 * It declares the same natives as SqGnss.kt (same class name, so the same JNI
 * symbols), loads libsqgnss_jni built for this machine, and checks geodesy,
 * stakeout, synthetic-sky PVT, the packed measurement feed path
 * (fillDemoEpoch → solveEpoch), and NTRIP → RTCM MSM framing (memio +
 * 127.0.0.1 loopback) — no phone GNSS and no remote caster. Run by
 * `make test-jni` in squirrelgnss.
 */
public final class SqGnss {
    static final int OUT_LEN = 9;
    static final int NTRIP_OUT_LEN = 12;
    static final int MAX_SATS = 32;

    static native boolean nativeLlhToEcef(double[] llh, double[] ecef);
    static native boolean nativeEcefToLlh(double[] ecef, double[] llh);
    static native boolean nativeStakeoutDelta(double[] fromLlh, double[] toLlh, double[] enu);
    static native boolean nativeDemoSolve(double lat, double lon, double heightM, double tGps,
                                          double clockBiasM, double noiseM, int seed, double[] out);
    static native int nativeFillDemoEpoch(double lat, double lon, double heightM, double tGps,
                                          double clockBiasM, double noiseM, int seed,
                                          double[] tReceiverOut, int[] prns, double[] ranges, double[] snrs);
    static native boolean nativeSolveEpoch(double tReceiver, double tEph, int[] prns, double[] ranges,
                                           double[] snrs, int n, double[] truthLlh, double[] out);
    static native boolean nativeNtripSynthMsm(int[] out);
    static native boolean nativeNtripFeedResponse(byte[] bytes, int[] out);
    static native boolean nativeNtripDial(String host, String port, String mount, String user,
                                          String pass, int ntripVersion, int[] out);
    static native boolean nativeNtripLoopbackMsm(int[] out);

    static int checks, failures;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    static boolean near(double a, double b, double tol) {
        return Math.abs(a - b) <= tol;
    }

    public static void main(String[] args) {
        System.load(args[0]);

        /* WGS84 equator: height 0 → X = a */
        double[] ecef = new double[3], llh = new double[3];
        check(nativeLlhToEcef(new double[] {0, 0, 0}, ecef), "llh→ecef equator");
        check(near(ecef[0], 6378137.0, 1e-6) && near(ecef[1], 0, 1e-6) && near(ecef[2], 0, 1e-6),
                "equator ECEF: " + ecef[0] + "," + ecef[1] + "," + ecef[2]);
        check(nativeEcefToLlh(ecef, llh), "ecef→llh equator");
        check(near(llh[0], 0, 1e-12) && near(llh[1], 0, 1e-12) && near(llh[2], 0, 1e-6),
                "equator round trip");

        /* Tokyo-ish site round trip */
        double[] tokyo = {35.681236, 139.767125, 40};
        check(nativeLlhToEcef(tokyo, ecef) && nativeEcefToLlh(ecef, llh), "tokyo round trip call");
        check(near(llh[0], tokyo[0], 1e-10) && near(llh[1], tokyo[1], 1e-10) && near(llh[2], tokyo[2], 1e-4),
                "tokyo round trip");

        check(!nativeLlhToEcef(new double[2], ecef), "llh too short");
        check(!nativeEcefToLlh(ecef, new double[2]), "llh out too short");
        check(!nativeLlhToEcef(null, ecef), "null llh");

        /* stakeout: 0.001° north ≈ 111 m */
        double[] enu = new double[3];
        check(nativeStakeoutDelta(new double[] {35, 139, 50}, new double[] {35.001, 139, 50}, enu), "stakeout");
        check(near(enu[1], 110.95, 0.1) && near(enu[0], 0, 1e-6) && near(enu[2], 0, 0.01),
                "stakeout ENU: e=" + enu[0] + " n=" + enu[1] + " u=" + enu[2]);
        check(!nativeStakeoutDelta(tokyo, tokyo, new double[2]), "enu too short");

        /* noiseless synthetic PVT at Tokyo: millimetre-class */
        double[] out = new double[OUT_LEN];
        check(nativeDemoSolve(tokyo[0], tokyo[1], tokyo[2], 100000, 1234.5, 0, 7, out), "demo solve");
        check(near(out[0], tokyo[0], 1e-8) && near(out[1], tokyo[1], 1e-8) && near(out[2], tokyo[2], 1e-3),
                "demo LLH");
        check(near(out[3], 1234.5, 1e-3), "demo clock: " + out[3]);
        check(out[4] >= 4, "demo n_used: " + out[4]);
        check(out[5] > 0 && out[6] > 0, "demo DOP");
        check(out[8] < 1e-3, "demo 3-D error m: " + out[8]);
        check(!nativeDemoSolve(tokyo[0], tokyo[1], tokyo[2], 100000, 15, 0, 1, new double[3]), "out too short");

        /* measurement feed: fill packed synthetic obs, then solveEpoch */
        int[] prns = new int[MAX_SATS];
        double[] ranges = new double[MAX_SATS];
        double[] snrs = new double[MAX_SATS];
        double[] tRx = new double[1];
        int n = nativeFillDemoEpoch(tokyo[0], tokyo[1], tokyo[2], 100000, 1234.5, 0, 7, tRx, prns, ranges, snrs);
        check(n >= 4, "fillDemoEpoch n=" + n);
        check(tRx[0] > 100000.0, "t_receiver includes clock: " + tRx[0]);
        for (int i = 0; i < n; i++) {
            check(prns[i] >= 1 && prns[i] <= 32, "prn[" + i + "]=" + prns[i]);
            check(ranges[i] > 1e7 && ranges[i] < 3e7, "range[" + i + "]=" + ranges[i]);
        }
        double[] out2 = new double[OUT_LEN];
        double tTrue = 100000;
        check(nativeSolveEpoch(tRx[0], tTrue, prns, ranges, snrs, n, tokyo, out2), "solveEpoch");
        check(near(out2[0], tokyo[0], 1e-8) && near(out2[1], tokyo[1], 1e-8), "solveEpoch LLH");
        check(near(out2[3], 1234.5, 1e-3), "solveEpoch clock");
        check(out2[8] < 1e-3, "solveEpoch 3-D err: " + out2[8]);
        check(near(out2[0], out[0], 1e-12) && near(out2[8], out[8], 1e-12), "fill+solve matches demoSolve");

        check(!nativeSolveEpoch(tRx[0], tTrue, prns, ranges, snrs, 3, tokyo, out2), "too few sats");
        check(nativeFillDemoEpoch(tokyo[0], tokyo[1], tokyo[2], 100000, 15, 0, 1, tRx, new int[2], new double[2],
                new double[2]) >= 0, "fill with short cap still returns");
        check(!nativeSolveEpoch(tRx[0], tTrue, null, ranges, snrs, n, tokyo, out2), "null prns");

        /* NTRIP memio synth: ICY 200 + MSM4 → framer → MSM parse */
        int[] nout = new int[NTRIP_OUT_LEN];
        check(nativeNtripSynthMsm(nout), "ntrip synth msm");
        check(nout[0] == 200 && nout[1] == 1, "ntrip synth status/kind");
        check(nout[2] > 0 && nout[3] >= 1, "ntrip synth body/frames");
        check(nout[4] == 1074 && nout[5] == 2001 && nout[6] == 345600000, "ntrip synth MSM4 header");
        check(nout[7] == 4 && nout[8] == 2 && nout[9] == 2 && nout[10] == 3, "ntrip synth MSM4 cells");
        check(nout[11] == 3, "ntrip synth category MSM");
        check(!nativeNtripSynthMsm(new int[3]), "ntrip synth out too short");

        /* Loopback TCP synthetic caster (127.0.0.1 only) */
        int[] lout = new int[NTRIP_OUT_LEN];
        check(nativeNtripLoopbackMsm(lout), "ntrip loopback msm");
        check(lout[4] == 1074 && lout[7] == 4 && lout[10] == 3, "ntrip loopback MSM4");

        /* Feed-response rejects empty / non-stream bytes */
        check(!nativeNtripFeedResponse(new byte[0], nout), "ntrip feed empty");
        check(!nativeNtripFeedResponse("HTTP/1.0 401 Unauthorized\r\n\r\n".getBytes(), nout),
                "ntrip feed 401");

        System.out.println("jni smoke: " + checks + " checks, " + failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }
}

package works.squirrel.gnss;

/*
 * Host smoke test for the Android JNI bridge (app/src/main/cpp/sqgnss_jni.c).
 * It declares the same natives as SqGnss.kt (same class name, so the same JNI
 * symbols), loads libsqgnss_jni built for this machine, and checks geodesy,
 * stakeout and a synthetic-sky PVT — no phone GNSS. Run by `make test-jni`
 * in squirrelgnss.
 */
public final class SqGnss {
    static final int OUT_LEN = 9;

    static native boolean nativeLlhToEcef(double[] llh, double[] ecef);
    static native boolean nativeEcefToLlh(double[] ecef, double[] llh);
    static native boolean nativeStakeoutDelta(double[] fromLlh, double[] toLlh, double[] enu);
    static native boolean nativeDemoSolve(double lat, double lon, double heightM, double tGps,
                                          double clockBiasM, double noiseM, int seed, double[] out);

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

        System.out.println("jni smoke: " + checks + " checks, " + failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }
}

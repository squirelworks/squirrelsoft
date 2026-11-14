package works.squirrel.sound;

/*
 * Host smoke test for the Android JNI bridge (app/src/main/cpp/sqdsp_jni.c).
 * It declares the same natives as SqDsp.kt (same class name, so the same JNI
 * symbols), loads libsqdsp_jni built for this machine, and checks every
 * entry point: the dBFS spectrum, the octave bands and the band levels, plus
 * the argument checks. Run by `make test-jni` in squirrelsound.
 */
public final class SqDsp {
    static native boolean nativeSpectrumDb(float[] windowed, float windowSum, float floorDb, float[] outDb);
    static native int nativeOctaveBands(int fraction, double fmin, double fmax, double[] centre, double[] lo, double[] hi);
    static native boolean nativeBandLevels(float[] magDb, double binHz, double[] lo, double[] hi, int nbands,
                                           double enbw, double floorDb, float[] outDb);
    static native int nativePeaks(float[] magDb, double sampleRate, int fftSize, double fMin, double fMax,
                                  double dbMin, int maxN, double minSepHz, double[] outHz, double[] outDb);

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

    /* periodic Hann, as Fft.hann in the app */
    static float[] hann(int n) {
        float[] w = new float[n];
        for (int i = 0; i < n; i++) w[i] = (float) (0.5 - 0.5 * Math.cos(2 * Math.PI * i / n));
        return w;
    }

    /* dBFS spectrum (n/2 bins) of a full-scale sine at hz, sampled at sr */
    static float[] sineSpectrum(int n, int sr, double hz) {
        float[] w = hann(n), x = new float[n];
        float sum = 0;
        for (int i = 0; i < n; i++) {
            x[i] = (float) (Math.sin(2 * Math.PI * hz * i / sr) * w[i]);
            sum += w[i];
        }
        float[] db = new float[n / 2];
        check(nativeSpectrumDb(x, sum, -140f, db), "spectrum of " + hz + " Hz");
        return db;
    }

    public static void main(String[] args) {
        System.load(args[0]);

        /* spectrum: a sine on bin 64 of 1024 reads 0 dBFS; the floor holds elsewhere */
        float[] db = sineSpectrum(1024, 1024, 64);
        check(near(db[64], 0, 0.05), "bin 64 at 0 dBFS: " + db[64]);
        check(db[300] >= -140f && db[300] < -100f, "far bins near the floor: " + db[300]);
        check(!nativeSpectrumDb(new float[1000], 500f, -140f, new float[500]), "not a power of two");
        check(!nativeSpectrumDb(new float[1024], 0f, -140f, new float[512]), "zero window sum");
        check(!nativeSpectrumDb(new float[1024], 1f, -140f, new float[600]), "output too long");

        /* octave bands, base 10 */
        double[] c = new double[64], lo = new double[64], hi = new double[64];
        int n = nativeOctaveBands(1, 20, 16000, c, lo, hi);
        check(n == 10, "10 octaves in 20..16k: " + n);
        check(near(c[0], 31.62, 0.01) && near(c[5], 1000, 1e-9) && near(c[9], 15848.93, 0.01), "octave centres");
        check(near(hi[4], lo[5], 1e-9), "edges meet");
        check(nativeOctaveBands(3, 19, 20000, c, lo, hi) == 31, "31 third octaves 20..20k");
        double[] c2 = new double[4], lo2 = new double[4], hi2 = new double[4];
        check(nativeOctaveBands(3, 19, 20000, c2, lo2, hi2) == 31 && near(c2[0], 19.95, 0.01), "small arrays: total, first written");
        check(nativeOctaveBands(0, 20, 100, c, lo, hi) == 0, "fraction 0");
        check(nativeOctaveBands(1, 20, 100, null, lo, hi) == -1, "null array");

        /* band levels: a full-scale 1 kHz sine reads ~0 dB in its octave */
        int fft = 4096, sr = 48000;
        float[] s = sineSpectrum(fft, sr, 1000);
        n = nativeOctaveBands(1, 20, 16000, c, lo, hi);
        float[] out = new float[n];
        check(nativeBandLevels(s, (double) sr / fft, lo, hi, n, 1.5, -140, out), "band levels");
        check(near(out[5], 0, 0.2), "1 kHz octave at 0 dB: " + out[5]);
        check(out[3] < -40, "250 Hz octave is only leakage: " + out[3]);
        boolean floored = true;
        for (float v : out) floored &= v >= -140f;
        check(floored, "nothing below the floor");
        float[] small = new float[2];
        check(!nativeBandLevels(s, (double) sr / fft, lo, hi, n, 1.5, -140, small), "output too short");
        check(!nativeBandLevels(s, 0, lo, hi, n, 1.5, -140, out), "zero bin width");
        check(nativeBandLevels(s, (double) sr / fft, lo, hi, 0, 1.5, -140, out), "no bands is fine");

        /* peaks: two tones → two labelled local maxima (sqdsp_local_peaks) */
        float[] two = new float[fft];
        float[] w = hann(fft);
        float sum = 0;
        for (int i = 0; i < fft; i++) {
            double t = Math.sin(2 * Math.PI * 1000.0 * i / sr) * 0.5
                    + Math.sin(2 * Math.PI * 5000.0 * i / sr) * 0.05;
            two[i] = (float) (t * w[i]);
            sum += w[i];
        }
        float[] mag = new float[fft / 2 + 1];
        check(nativeSpectrumDb(two, sum, -140f, mag), "two-tone spectrum");
        double[] phz = new double[5], pdb = new double[5];
        int pn = nativePeaks(mag, sr, fft, 20, 20000, -120, 5, 100, phz, pdb);
        check(pn == 2, "two peaks: " + pn);
        check(pn >= 2 && near(phz[0], 1000, 2) && near(phz[1], 5000, 2) && pdb[0] > pdb[1],
                "peaks loudest first near 1 kHz then 5 kHz");
        check(nativePeaks(mag, sr, fft, 20, 20000, -120, 1, 100, phz, pdb) == 1
                && near(phz[0], 1000, 2), "maxN 1 keeps the loudest");
        check(nativePeaks(mag, sr, fft, 2000, 20000, -120, 5, 100, phz, pdb) == 1
                && near(phz[0], 5000, 2), "peaks only inside the range");
        check(nativePeaks(null, sr, fft, 20, 20000, -120, 5, 100, phz, pdb) == 0, "null spectrum");
        check(nativePeaks(mag, 0, fft, 20, 20000, -120, 5, 100, phz, pdb) == 0, "zero sample rate");

        System.out.println("jni smoke: " + checks + " checks, " + failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }
}

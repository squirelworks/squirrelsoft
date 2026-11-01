#include "sqnr.h"
#include "synth.h"
#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

static void test_fft_impulse(void) {
    size_t n = 16;
    double x[16];
    memset(x, 0, sizeof(x));
    x[0] = 1.0;
    sqnr_realft(x, n, 1);
    /* Unit impulse ⇒ flat spectrum: DC/Nyquist = 1, other bins Re=1 Im=0. */
    expect(fabs(x[0] - 1.0) < 1e-9, "realft impulse DC");
    expect(fabs(x[1] - 1.0) < 1e-9, "realft impulse Nyquist");
    for (size_t k = 1; k < n / 2; k++) {
        expect(fabs(x[2 * k] - 1.0) < 1e-9 && fabs(x[2 * k + 1]) < 1e-9,
               "realft impulse flat bin");
    }
    /* Round-trip */
    sqnr_realft(x, n, -1);
    for (size_t i = 0; i < n; i++) x[i] /= (double)(n / 2);
    expect(fabs(x[0] - 1.0) < 1e-9, "realft inverse impulse");
    for (size_t i = 1; i < n; i++) expect(fabs(x[i]) < 1e-9, "realft inverse zeros");
}

static void test_tone_peak(void) {
    double fs = 8000.0;
    size_t n = 2048;
    double *x = (double *)malloc(n * sizeof(double));
    synth_tone(x, n, fs, 440.0, 1, 0.5);
    double *psd = (double *)malloc((n / 2 + 1) * sizeof(double));
    double *freq = (double *)malloc((n / 2 + 1) * sizeof(double));
    double *scratch = (double *)malloc(n * 3 * sizeof(double));
    size_t nb = sqnr_periodogram(x, n, fs, SQNR_WIN_HANN, psd, freq, scratch);
    double *mag = (double *)malloc(nb * sizeof(double));
    for (size_t i = 0; i < nb; i++) mag[i] = sqrt(psd[i]);
    double peak = sqnr_peak_frequency(mag, freq, nb);
    expect(fabs(peak - 440.0) < 2.0, "440 Hz peak within 2 Hz");

    size_t np2 = 1;
    while (np2 < n) np2 <<= 1;
    np2 <<= 1;
    double *acscratch = (double *)malloc((n + np2 + 8) * sizeof(double));
    double f0 = sqnr_pitch_autocorr(x, n, fs, 80.0, 800.0, acscratch);
    expect(fabs(f0 - 440.0) < 3.0, "autocorr pitch near 440");

    free(x);
    free(psd);
    free(freq);
    free(scratch);
    free(mag);
    free(acscratch);
}

static void test_thd_harmonics(void) {
    double fs = 16000.0;
    size_t n = 4096;
    double *x = (double *)malloc(n * sizeof(double));
    synth_tone(x, n, fs, 500.0, 3, 0.4);
    double *psd = (double *)malloc((n / 2 + 1) * sizeof(double));
    double *freq = (double *)malloc((n / 2 + 1) * sizeof(double));
    double *scratch = (double *)malloc(n * 3 * sizeof(double));
    size_t nb = sqnr_periodogram(x, n, fs, SQNR_WIN_BLACKMAN, psd, freq, scratch);
    double *mag = (double *)malloc(nb * sizeof(double));
    for (size_t i = 0; i < nb; i++) mag[i] = sqrt(psd[i]);
    double thd = sqnr_thd(mag, freq, nb, 500.0, 5);
    /* harmonics 1/2 + 1/3 amplitude => power (0.5^2 + ~0.333^2) / 1^2 */
    expect(thd > 0.4 && thd < 0.8, "thd in expected band for 1/h series");
    free(x);
    free(psd);
    free(freq);
    free(scratch);
    free(mag);
}

static void test_wav_roundtrip(void) {
    double fs = 44100.0;
    size_t n = 1000;
    double *x = (double *)malloc(n * sizeof(double));
    synth_tone(x, n, fs, 1000.0, 1, 0.25);
    const char *path = "/tmp/sqnr_wav_test.wav";
    expect(wav_write_pcm16(path, x, n, (uint32_t)fs) == 0, "wav write");
    WavBuffer w;
    expect(wav_read(path, &w) == 0, "wav read");
    expect(w.n_samples == n, "wav n");
    expect(w.sample_rate == 44100, "wav rate");
    expect(fabs(w.samples[10] - x[10]) < 1e-3, "wav sample approx");
    wav_free(&w);
    free(x);
}

static void test_welch_and_linfit(void) {
    double fs = 4000.0;
    size_t n = 8192;
    double *x = (double *)malloc(n * sizeof(double));
    synth_tone(x, n, fs, 250.0, 1, 0.5);
    size_t seg = 1024;
    size_t nb = seg / 2 + 1;
    double *psd = (double *)malloc(nb * sizeof(double));
    double *freq = (double *)malloc(nb * sizeof(double));
    double *scratch = (double *)malloc((nb * 2 + seg * 3) * sizeof(double));
    size_t got = sqnr_welch_psd(x, n, fs, seg, 0.5, SQNR_WIN_HANN, psd, freq, scratch);
    expect(got == nb, "welch bins");
    size_t kmax = 1;
    for (size_t k = 1; k < got; k++)
        if (psd[k] > psd[kmax]) kmax = k;
    expect(fabs(freq[kmax] - 250.0) < 5.0, "welch peak ~250");

    double xs[4] = {0, 1, 2, 3}, ys[4] = {1, 3, 5, 7}, a, b;
    expect(sqnr_linfit(xs, ys, 4, &a, &b) == 0, "linfit");
    expect(fabs(a - 1.0) < 1e-9 && fabs(b - 2.0) < 1e-9, "linfit coeffs");
    free(x);
    free(psd);
    free(freq);
    free(scratch);
}

int main(void) {
    test_fft_impulse();
    test_tone_peak();
    test_thd_harmonics();
    test_wav_roundtrip();
    test_welch_and_linfit();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("squirrelsound / sqnr tests ok");
    return 0;
}

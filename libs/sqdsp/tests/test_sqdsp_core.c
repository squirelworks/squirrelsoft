/* Core tests for libs/sqdsp: FFT against a naive DFT, impulse response. */
#include "sqdsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

/* Slow O(n²) DFT reference — independent of sqdsp_fft. */
static void naive_dft(const double *re_in, const double *im_in, size_t n,
                      double *re_out, double *im_out, int forward) {
    double sign = forward ? -1.0 : 1.0;
    for (size_t k = 0; k < n; k++) {
        double sr = 0.0, si = 0.0;
        for (size_t t = 0; t < n; t++) {
            double ang = sign * 2.0 * M_PI * (double)k * (double)t / (double)n;
            double c = cos(ang), s = sin(ang);
            sr += re_in[t] * c - im_in[t] * s;
            si += re_in[t] * s + im_in[t] * c;
        }
        re_out[k] = sr;
        im_out[k] = si;
    }
}

static void test_fft_vs_naive_dft(void) {
    const size_t n = 32;
    double data[64];
    double re_in[32], im_in[32], re_ref[32], im_ref[32];
    unsigned seed = 0xC0FFEEu;
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1664525u + 1013904223u;
        re_in[i] = ((int)(seed >> 16) % 2001 - 1000) / 1000.0;
        seed = seed * 1664525u + 1013904223u;
        im_in[i] = ((int)(seed >> 16) % 2001 - 1000) / 1000.0;
        data[2 * i] = re_in[i];
        data[2 * i + 1] = im_in[i];
    }
    naive_dft(re_in, im_in, n, re_ref, im_ref, 1);
    sqdsp_fft(data, n, 1);
    double err = 0.0;
    for (size_t i = 0; i < n; i++) {
        double dr = data[2 * i] - re_ref[i];
        double di = data[2 * i + 1] - im_ref[i];
        err += dr * dr + di * di;
    }
    expect(err < 1e-16 * (double)n, "fft matches naive DFT on random input");

    /* Inverse: FFT^{-1}(FFT(x)) / n ≈ x */
    sqdsp_fft(data, n, -1);
    err = 0.0;
    for (size_t i = 0; i < n; i++) {
        double dr = data[2 * i] / (double)n - re_in[i];
        double di = data[2 * i + 1] / (double)n - im_in[i];
        err += dr * dr + di * di;
    }
    expect(err < 1e-16 * (double)n, "fft inverse round-trip");
}

static void test_fft_impulse(void) {
    size_t n = 16;
    double x[16];
    memset(x, 0, sizeof(x));
    x[0] = 1.0;
    sqdsp_rfft(x, n, 1);
    /* Unit impulse ⇒ flat spectrum: DC/Nyquist = 1, other bins Re=1 Im=0. */
    expect(fabs(x[0] - 1.0) < 1e-9, "rfft impulse DC");
    expect(fabs(x[1] - 1.0) < 1e-9, "rfft impulse Nyquist");
    for (size_t k = 1; k < n / 2; k++) {
        expect(fabs(x[2 * k] - 1.0) < 1e-9 && fabs(x[2 * k + 1]) < 1e-9,
               "rfft impulse flat bin");
    }
    /* Round-trip */
    sqdsp_rfft(x, n, -1);
    for (size_t i = 0; i < n; i++) x[i] /= (double)(n / 2);
    expect(fabs(x[0] - 1.0) < 1e-9, "rfft inverse impulse");
    for (size_t i = 1; i < n; i++) expect(fabs(x[i]) < 1e-9, "rfft inverse zeros");
}

static void test_octave_bands(void) {
    double c[64], lo[64], hi[64];
    /* nominal third-octave centres 20 Hz .. 20 kHz: 31 bands */
    size_t n = sqdsp_octave_bands(3, 19.0, 21000.0, c, lo, hi, 64);
    expect(n == 31, "31 third-octave bands 20 Hz..20 kHz");
    expect(fabs(c[17] - 1000.0) < 1e-6, "third-octave 1 kHz centre");
    expect(fabs(c[0] - 19.95) < 0.01 && fabs(c[30] - 19952.6) < 0.5, "third-octave ends");
    for (size_t i = 0; i + 1 < n; i++)
        expect(fabs(hi[i] - lo[i + 1]) < 1e-6 * hi[i], "bands touch");
    expect(fabs(lo[17] * hi[17] - 1e6) < 1e-3, "edges geometric around the centre");
    n = sqdsp_octave_bands(1, 20.0, 20000.0, c, lo, hi, 64);
    expect(n == 10 && fabs(c[0] - 31.62) < 0.01 && fabs(c[5] - 1000.0) < 1e-6,
           "10 octave bands 31.5 Hz..16 kHz");
    expect(sqdsp_octave_bands(3, 19.0, 21000.0, NULL, NULL, NULL, 0) == 31, "count only");
    expect(sqdsp_octave_bands(6, 900.0, 1100.0, c, NULL, NULL, 64) == 2 &&
               c[0] < 1000.0 && c[1] > 1000.0, "even fraction straddles 1 kHz");
    expect(sqdsp_octave_bands(0, 20, 20000, c, lo, hi, 64) == 0, "bad fraction");

    /* a -6 dBFS 1 kHz tone (Hann) reads about -6 dB in its band */
    enum { N = 4096 };
    static double x[N], w[N], mag[N / 2 + 1], freq[N / 2 + 1], scratch[2 * N], db[64];
    double fs = 48000.0;
    sqdsp_window(w, N, SQDSP_WIN_HANN, 0);
    double ws = 0;
    for (int i = 0; i < N; i++) {
        x[i] = 0.5 * sin(2 * M_PI * 1000.0 * i / fs) * w[i];
        ws += w[i];
    }
    expect(fabs(sqdsp_window_enbw(w, N) - 1.5) < 1e-3, "Hann ENBW 1.5 bins");
    size_t nb = sqdsp_magnitude_spectrum(x, N, mag, scratch);
    for (size_t k = 0; k < nb; k++) {
        freq[k] = k * fs / N;
        mag[k] = 20 * log10(mag[k] * 2.0 / ws + 1e-20);
    }
    n = sqdsp_octave_bands(3, 19, 21000, c, lo, hi, 64);      /* 20 Hz is band 0 */
    sqdsp_band_levels_db(mag, freq, nb, lo, hi, n, sqdsp_window_enbw(w, N), -140, db);
    expect(fabs(db[17] + 6.02) < 0.3, "tone level in its third-octave band");
    expect(db[14] < -60 && db[20] < -60, "neighbouring bands quiet");
    expect(db[0] == -140, "a band with no bins gets the floor");
}

int main(void) {
    test_fft_vs_naive_dft();
    test_fft_impulse();
    test_octave_bands();
    if (failures) {
        fprintf(stderr, "%d sqdsp core failure(s)\n", failures);
        return 1;
    }
    puts("libs/sqdsp core tests ok");
    return 0;
}

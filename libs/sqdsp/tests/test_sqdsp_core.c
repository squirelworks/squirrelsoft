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

int main(void) {
    test_fft_vs_naive_dft();
    test_fft_impulse();
    if (failures) {
        fprintf(stderr, "%d sqdsp core failure(s)\n", failures);
        return 1;
    }
    puts("libs/sqdsp core tests ok");
    return 0;
}

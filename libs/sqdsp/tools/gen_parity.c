/* Writes the FFT parity fixture shared with the Kotlin SquirrelSound app.
 *
 * Usage: gen_parity <out.txt>          write the fixture
 *        gen_parity --check <file>     verify a committed fixture (1e-9 tolerance,
 *                                      so libm last-bit differences don't fail it)
 *
 * Format (one number per line, %.17g):
 *   n
 *   n raw samples (deterministic tones + LCG noise)
 *   n Hann window coefficients (sqdsp_window SQDSP_WIN_HANN)
 *   n/2+1 magnitudes |X_k| of the windowed samples (sqdsp_magnitude_spectrum)
 *
 * The Kotlin test (squirrelsound/android ... FftParityTest) runs Fft.hann and
 * Fft.forward on the same samples and must agree within 1e-4 of the peak. */
#include "sqdsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define N 1024

static int check(const char *path, const double *vals, size_t count) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 1;
    }
    size_t bad = 0;
    for (size_t i = 0; i < count; i++) {
        double v;
        if (fscanf(f, "%lf", &v) != 1) {
            fprintf(stderr, "%s: short file at value %zu\n", path, i);
            fclose(f);
            return 1;
        }
        double tol = 1e-9 * (fabs(vals[i]) > 1.0 ? fabs(vals[i]) : 1.0);
        if (fabs(v - vals[i]) > tol) bad++;
    }
    fclose(f);
    if (bad) {
        fprintf(stderr, "%s is stale (%zu values differ): run make -C libs/sqdsp fixture\n",
                path, bad);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int checking = argc == 3 && strcmp(argv[1], "--check") == 0;
    if (argc != 2 && !checking) {
        fprintf(stderr, "usage: %s <out.txt> | --check <file>\n", argv[0]);
        return 2;
    }
    static double x[N], w[N], xw[N], mag[N / 2 + 1], scratch[N];
    unsigned int seed = 12345u;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        double noise = ((double)((seed >> 8) & 0xFFFF) / 65535.0 - 0.5) * 0.02;
        x[i] = 0.5 * sin(2.0 * M_PI * 37.0 * i / N) +
               0.25 * sin(2.0 * M_PI * 101.3 * i / N + 0.7) +
               0.05 * cos(2.0 * M_PI * 300.0 * i / N) + noise;
    }
    sqdsp_window(w, N, SQDSP_WIN_HANN, 0.0);
    for (int i = 0; i < N; i++) xw[i] = x[i] * w[i];
    if (sqdsp_magnitude_spectrum(xw, N, mag, scratch) != N / 2 + 1) return 1;

    if (checking) {
        static double all[1 + N + N + N / 2 + 1];
        size_t k = 0;
        all[k++] = N;
        for (int i = 0; i < N; i++) all[k++] = x[i];
        for (int i = 0; i < N; i++) all[k++] = w[i];
        for (int i = 0; i <= N / 2; i++) all[k++] = mag[i];
        return check(argv[2], all, k);
    }

    FILE *f = fopen(argv[1], "w");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fprintf(f, "%d\n", N);
    for (int i = 0; i < N; i++) fprintf(f, "%.17g\n", x[i]);
    for (int i = 0; i < N; i++) fprintf(f, "%.17g\n", w[i]);
    for (int i = 0; i <= N / 2; i++) fprintf(f, "%.17g\n", mag[i]);
    return fclose(f) == 0 ? 0 : 1;
}

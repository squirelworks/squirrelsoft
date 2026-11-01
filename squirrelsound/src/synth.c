#include "synth.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void synth_tone(double *out, size_t n, double fs, double freq, int harmonics,
                double amp) {
    if (harmonics < 1) harmonics = 1;
    for (size_t i = 0; i < n; i++) {
        double t = (double)i / fs;
        double s = 0.0;
        for (int h = 1; h <= harmonics; h++) {
            double a = amp / (double)h;
            s += a * sin(2.0 * M_PI * freq * (double)h * t);
        }
        out[i] = s;
    }
}

void synth_noise(double *out, size_t n, double amp, unsigned seed) {
    unsigned s = seed ? seed : 1u;
    for (size_t i = 0; i < n; i++) {
        s = s * 1664525u + 1013904223u;
        double u = (double)(s >> 8) / (double)(1u << 24);
        out[i] = amp * (2.0 * u - 1.0);
    }
}

void synth_chirp(double *out, size_t n, double fs, double f0, double f1, double amp) {
    double T = (double)n / fs;
    for (size_t i = 0; i < n; i++) {
        double t = (double)i / fs;
        double f = f0 + (f1 - f0) * (t / T);
        /* phase via instantaneous frequency integral */
        double phase = 2.0 * M_PI * (f0 * t + 0.5 * (f1 - f0) * t * t / T);
        (void)f;
        out[i] = amp * sin(phase);
    }
}

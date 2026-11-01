#ifndef SYNTH_H
#define SYNTH_H

#include <stddef.h>
#include <stdint.h>

/* Generate seconds of tone at freq with optional harmonic count (1=fundamental). */
void synth_tone(double *out, size_t n, double fs, double freq, int harmonics,
                double amp);

void synth_noise(double *out, size_t n, double amp, unsigned seed);
void synth_chirp(double *out, size_t n, double fs, double f0, double f1, double amp);

#endif

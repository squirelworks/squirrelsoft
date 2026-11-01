#ifndef SPECTROGRAM_H
#define SPECTROGRAM_H

#include "sqnr.h"

#include <stddef.h>

int sqsound_write_spectrogram_svg(const char *path, const double *x, size_t n, double fs,
                                  size_t fft, double overlap, SqnrWindow win);

#endif

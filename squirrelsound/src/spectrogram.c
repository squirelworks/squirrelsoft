#include "sqdsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Write a simple SVG spectrogram heatmap. Returns 0 on success. */
int sqsound_write_spectrogram_svg(const char *path, const double *x, size_t n, double fs,
                                  size_t fft, double overlap, SqdspWindow win) {
    (void)fs; /* reserved for axis labeling in later SVG revisions */
    if (!sqdsp_is_pow2(fft) || fft > n) return -1;
    size_t hop = (size_t)((1.0 - overlap) * (double)fft);
    if (hop < 1) hop = 1;
    size_t nframes = 0;
    for (size_t s = 0; s + fft <= n; s += hop) nframes++;
    if (nframes == 0) return -1;
    size_t nb = fft / 2 + 1;
    double *mag = (double *)calloc(nframes * nb, sizeof(double));
    double *scratch = (double *)malloc((fft * 3) * sizeof(double));
    double *w = scratch;
    double *buf = scratch + fft;
    double *fftbuf = scratch + 2 * fft;
    if (!mag || !scratch) {
        free(mag);
        free(scratch);
        return -1;
    }
    sqdsp_window(w, fft, win, 8.6);
    size_t fi = 0;
    double maxv = 1e-20;
    for (size_t s = 0; s + fft <= n; s += hop, fi++) {
        memcpy(buf, x + s, fft * sizeof(double));
        sqdsp_apply_window(buf, w, fft);
        size_t got = sqdsp_magnitude_spectrum(buf, fft, mag + fi * nb, fftbuf);
        (void)got;
        for (size_t k = 0; k < nb; k++)
            if (mag[fi * nb + k] > maxv) maxv = mag[fi * nb + k];
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        free(mag);
        free(scratch);
        return -1;
    }
    int W = (int)nframes;
    int H = (int)(nb > 512 ? 512 : nb);
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\">\n",
            W, H);
    fprintf(f, "<rect width=\"100%%\" height=\"100%%\" fill=\"#0b1020\"/>\n");
    for (size_t t = 0; t < nframes; t++) {
        for (int y = 0; y < H; y++) {
            size_t k = (size_t)((double)y / (double)H * (double)(nb - 1));
            /* low freq at bottom */
            k = (nb - 1) - k;
            double v = mag[t * nb + k] / maxv;
            double db = 20.0 * log10(v + 1e-12);
            if (db < -80.0) db = -80.0;
            double tnorm = (db + 80.0) / 80.0;
            int r = (int)(20 + 220 * tnorm);
            int g = (int)(30 + 180 * tnorm * tnorm);
            int b = (int)(80 + 100 * (1.0 - tnorm));
            fprintf(f, "<rect x=\"%zu\" y=\"%d\" width=\"1\" height=\"1\" fill=\"rgb(%d,%d,%d)\"/>\n",
                    t, y, r, g, b);
        }
    }
    fprintf(f, "</svg>\n");
    fclose(f);
    free(mag);
    free(scratch);
    return 0;
}

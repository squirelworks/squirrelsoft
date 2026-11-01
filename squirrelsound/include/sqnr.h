/*
 * sqnr — Squirrel spectral DSP primitives (NR-inspired feature set only).
 * Clean-room radix-2 FFT / real packing / windows — not Numerical Recipes
 * book source, and not a transcription of four1/realft/bessi0.
 */
#ifndef SQNR_H
#define SQNR_H

#include <stddef.h>

typedef enum {
    SQNR_WIN_RECT = 0,
    SQNR_WIN_HANN,
    SQNR_WIN_HAMMING,
    SQNR_WIN_BLACKMAN,
    SQNR_WIN_KAISER,
    SQNR_WIN_FLATTOP
} SqnrWindow;

/* Return 1 if n is power of two. */
int sqnr_is_pow2(size_t n);

/* In-place complex radix-2 FFT. data is [re0,im0,re1,im1,...] length 2*n.
 * isign = +1 forward, -1 inverse (no 1/n scaling on inverse — caller divides). */
void sqnr_four1(double *data, size_t nn, int isign);

/* Real-valued FFT of n samples (n power of 2) into packed Hermitian form
 * of length n: data[0]=Re F0, data[1]=Re F{n/2}, then Re/Im pairs. */
void sqnr_realft(double *data, size_t n, int isign);

/* Fill window of length n. kaiser_beta used only for Kaiser. */
void sqnr_window(double *w, size_t n, SqnrWindow kind, double kaiser_beta);

/* Apply window to real signal (in place). */
void sqnr_apply_window(double *x, const double *w, size_t n);

/* One-sided magnitude spectrum |X[k]| for k=0..n/2 inclusive.
 * x is real length n (destroyed/copied internally via buffer owned by caller
 * if pass scratch of length n). Returns bins written (n/2+1). */
size_t sqnr_magnitude_spectrum(const double *x, size_t n, double *mag_out,
                               double *scratch);

/* Periodogram PSD (one-sided, density-ish): |X|^2 / (fs * U) with U=sum(w^2). */
size_t sqnr_periodogram(const double *x, size_t n, double fs, SqnrWindow win,
                        double *psd_out, double *freq_out, double *scratch);

/* Welch PSD: segment length seg, overlap fraction in [0,0.95]. */
size_t sqnr_welch_psd(const double *x, size_t n, double fs, size_t seg,
                      double overlap, SqnrWindow win, double *psd_out,
                      double *freq_out, double *scratch);

/* Autocorrelation via FFT (biased). out length n. scratch length >= 4*np2. */
void sqnr_correlate(const double *x, size_t n, double *out, double *scratch);

/* Peak frequency from magnitude spectrum; returns Hz. */
double sqnr_peak_frequency(const double *mag, const double *freq, size_t nbins);

/* Parabolic interpolation around discrete peak bin. */
double sqnr_parabolic_bin(const double *mag, size_t nbins, size_t k);

/* Autocorr F0 estimate in [fmin,fmax]. */
double sqnr_pitch_autocorr(const double *x, size_t n, double fs, double fmin,
                           double fmax, double *scratch);

/* THD for harmonics 2..max_harmonic relative to fund_hz. */
double sqnr_thd(const double *mag, const double *freq, size_t nbins,
                double fund_hz, int max_harmonic);

/* Band SNR: signal power in ±bw_hz around fund vs rest. */
double sqnr_snr_db(const double *psd, const double *freq, size_t nbins,
                   double fund_hz, double bw_hz);

/* Linear regression y = a + b x ; returns 0. */
int sqnr_linfit(const double *x, const double *y, size_t n, double *a, double *b);

SqnrWindow sqnr_window_parse(const char *name);

#endif

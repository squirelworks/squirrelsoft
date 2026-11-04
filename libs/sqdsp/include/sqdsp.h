/*
 * sqdsp — SquirrelSound spectral DSP primitives (clean-room).
 * Radix-2 complex FFT, packed real FFT, windows, PSD, pitch/THD helpers.
 */
#ifndef SQDSP_H
#define SQDSP_H

#include <stddef.h>

typedef enum {
    SQDSP_WIN_RECT = 0,
    SQDSP_WIN_HANN,
    SQDSP_WIN_HAMMING,
    SQDSP_WIN_BLACKMAN,
    SQDSP_WIN_KAISER,
    SQDSP_WIN_FLATTOP
} SqdspWindow;

/* Return 1 if n is power of two. */
int sqdsp_is_pow2(size_t n);

/* In-place complex radix-2 FFT. data is [re0,im0,re1,im1,...] length 2*nn.
 * isign = +1 forward, -1 inverse (no 1/nn scaling on inverse — caller divides). */
void sqdsp_fft(double *data, size_t nn, int isign);

/* Real-valued FFT of n samples (n power of 2) into packed Hermitian form
 * of length n: data[0]=Re F0, data[1]=Re F{n/2}, then Re/Im pairs. */
void sqdsp_rfft(double *data, size_t n, int isign);

/* Fill window of length n. kaiser_beta used only for Kaiser. */
void sqdsp_window(double *w, size_t n, SqdspWindow kind, double kaiser_beta);

/* Apply window to real signal (in place). */
void sqdsp_apply_window(double *x, const double *w, size_t n);

/* One-sided magnitude spectrum |X[k]| for k=0..n/2 inclusive.
 * scratch must be length n. Returns bins written (n/2+1). */
size_t sqdsp_magnitude_spectrum(const double *x, size_t n, double *mag_out,
                                double *scratch);

/* Periodogram PSD (one-sided): |X|^2 / (fs * U) with U=sum(w^2). */
size_t sqdsp_periodogram(const double *x, size_t n, double fs, SqdspWindow win,
                         double *psd_out, double *freq_out, double *scratch);

/* Welch PSD: segment length seg, overlap fraction in [0,0.95]. */
size_t sqdsp_welch_psd(const double *x, size_t n, double fs, size_t seg,
                       double overlap, SqdspWindow win, double *psd_out,
                       double *freq_out, double *scratch);

/* Autocorrelation via FFT (biased). out length n. */
void sqdsp_correlate(const double *x, size_t n, double *out, double *scratch);

/* Peak frequency from magnitude spectrum; returns Hz. */
double sqdsp_peak_frequency(const double *mag, const double *freq, size_t nbins);

/* Parabolic interpolation around discrete peak bin. */
double sqdsp_parabolic_bin(const double *mag, size_t nbins, size_t k);

/* Autocorr F0 estimate in [fmin,fmax]. */
double sqdsp_pitch_autocorr(const double *x, size_t n, double fs, double fmin,
                            double fmax, double *scratch);

/* THD for harmonics 2..max_harmonic relative to fund_hz. */
double sqdsp_thd(const double *mag, const double *freq, size_t nbins,
                 double fund_hz, int max_harmonic);

/* Band SNR: signal power in ±bw_hz around fund vs rest. */
double sqdsp_snr_db(const double *psd, const double *freq, size_t nbins,
                    double fund_hz, double bw_hz);

/* Linear regression y = a + b x ; returns 0. */
int sqdsp_linfit(const double *x, const double *y, size_t n, double *a, double *b);

SqdspWindow sqdsp_window_parse(const char *name);

#endif

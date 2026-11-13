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

/* Equivalent noise bandwidth of window w, in bins: n * sum(w^2) / sum(w)^2
 * (1.0 rectangular, 1.5 Hann). */
double sqdsp_window_enbw(const double *w, size_t n);

/* Fractional-octave bands, base 10 (IEC 61260-1): centres 1000 * G^(x/b)
 * with G = 10^(3/10), edges at G^(+-1/(2b)) around each centre. fraction
 * is b: 1 for octaves, 3 for third octaves. Writes the bands whose centre
 * lies in [fmin, fmax] (up to cap) and returns how many there are. Any of
 * the output arrays may be NULL. */
size_t sqdsp_octave_bands(int fraction, double fmin, double fmax, double *centre,
                          double *lo, double *hi, size_t cap);

/* Band levels from a dB spectrum (one value per bin, e.g. dBFS of a
 * sine's amplitude): sums the power of the bins whose frequency lies in
 * [lo, hi) and divides by enbw (the window's ENBW in bins) so a pure tone
 * reads its own level. Bands with no bins, or below floor_db, get floor_db. */
void sqdsp_band_levels_db(const double *mag_db, const double *freq, size_t nbins,
                          const double *lo, const double *hi, size_t nbands, double enbw,
                          double floor_db, double *out_db);

/* ---- multi-resolution spectrum ----
 * One FFT size can't serve the whole band: the bass needs long frames for
 * fine bins, the treble wants short ones to follow fast changes. Level k
 * takes the newest n / 4^k samples of a frame of n, so each level is four
 * times shorter (and its bins four times wider) than the last. A frequency
 * f uses the shortest level whose bin is still at most f / q wide (about
 * constant relative resolution), and neighbouring levels are crossfaded in
 * dB over an octave around each crossover, so the joins don't step. */
#define SQDSP_MR_MAX_LEVELS 4

typedef struct {
    int levels;                          /* 1 to SQDSP_MR_MAX_LEVELS */
    size_t size[SQDSP_MR_MAX_LEVELS];    /* n, n/4, n/16, n/64 */
    double cross_hz[SQDSP_MR_MAX_LEVELS]; /* level k takes over from k-1 here (k >= 1) */
    size_t n;
    double fs;
} SqdspMultires;

/* Plan levels for a frame of n samples at fs (n a power of two; levels are
 * dropped while the shortest would be under 64 samples). q: bins per
 * frequency, 32 is a good start. Returns the levels, or 0 for bad input. */
int sqdsp_multires_plan(SqdspMultires *p, size_t n, double fs, int levels, double q);

/* The stitched spectrum of the frame x (p->n samples) in dBFS on the
 * n-point grid (n/2 + 1 values, bin k at k fs / n): a full-scale sine reads
 * 0 dB at every level, whatever the window. Each level is windowed with
 * win over its own samples. work needs 3 n doubles. Returns n/2 + 1. A
 * level's noise floor per bin is higher (6 dB for each step) since its bins
 * are wider: tones read true, noise does not. */
size_t sqdsp_multires_db(const SqdspMultires *p, const double *x, SqdspWindow win, double *db_out,
                         double *work);
/* How many grid bins one bin of the level showing frequency hz spans (n
 * over that level's length; blended as the power is in a crossfade), or 0
 * for bad input. A tone spreads over that many times more grid bins than
 * on one n-point FFT, so a band power sum divides each bin by it as well
 * as by the window's ENBW. */
double sqdsp_multires_spread(const SqdspMultires *p, double hz);

#endif

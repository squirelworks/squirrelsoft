#include "sqnr.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int sqnr_is_pow2(size_t n) { return n && !(n & (n - 1)); }

/*
 * Complex radix-2 FFT — original 0-based iterative Cooley–Tukey.
 * Layout: data[2*k]=Re, data[2*k+1]=Im for k = 0 .. nn-1.
 * isign > 0 → forward (e^{-2πi}), isign < 0 → inverse (no 1/nn scale).
 */
void sqnr_four1(double *data, size_t nn, int isign) {
    if (nn < 2 || !sqnr_is_pow2(nn)) return;

    /* Bit-reversal permutation (0-based complex indices). */
    size_t j = 0;
    for (size_t i = 1; i < nn; i++) {
        size_t bit = nn >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            size_t ia = i << 1, ja = j << 1;
            double tr = data[ia], ti = data[ia + 1];
            data[ia] = data[ja];
            data[ia + 1] = data[ja + 1];
            data[ja] = tr;
            data[ja + 1] = ti;
        }
    }

    /* Precompute unit-stride twiddles for the largest stage, reuse by stride. */
    double *tw_re = (double *)malloc(nn * sizeof(double));
    double *tw_im = (double *)malloc(nn * sizeof(double));
    if (!tw_re || !tw_im) {
        free(tw_re);
        free(tw_im);
        return;
    }
    {
        double sign = (isign >= 0) ? -1.0 : 1.0;
        for (size_t k = 0; k < nn; k++) {
            double ang = sign * 2.0 * M_PI * (double)k / (double)nn;
            tw_re[k] = cos(ang);
            tw_im[k] = sin(ang);
        }
    }

    for (size_t len = 2; len <= nn; len <<= 1) {
        size_t half = len >> 1;
        size_t tw_stride = nn / len;
        for (size_t start = 0; start < nn; start += len) {
            for (size_t k = 0; k < half; k++) {
                size_t tix = k * tw_stride;
                size_t u = (start + k) << 1;
                size_t v = (start + k + half) << 1;
                double wr = tw_re[tix], wi = tw_im[tix];
                double vr = data[v] * wr - data[v + 1] * wi;
                double vi = data[v] * wi + data[v + 1] * wr;
                double ur = data[u], ui = data[u + 1];
                data[u] = ur + vr;
                data[u + 1] = ui + vi;
                data[v] = ur - vr;
                data[v + 1] = ui - vi;
            }
        }
    }
    free(tw_re);
    free(tw_im);
}

/*
 * Real FFT packing (our convention, matches callers):
 *   data[0]       = Re F[0]
 *   data[1]       = Re F[n/2]
 *   data[2*k]     = Re F[k]
 *   data[2*k + 1] = Im F[k]   for k = 1 .. n/2-1
 *
 * Forward uses a length-n/2 complex FFT on even/odd split; inverse undoes it.
 * After inverse, divide by (n/2) to recover the original real sequence.
 */
void sqnr_realft(double *data, size_t n, int isign) {
    if (n < 4 || !sqnr_is_pow2(n)) return;
    const size_t half = n >> 1;
    const size_t quarter = n >> 2;

    if (isign >= 0) {
        /* Even/odd split → complex vector of length half, then FFT. */
        sqnr_four1(data, half, 1);

        double *re = (double *)malloc(half * sizeof(double));
        double *im = (double *)malloc(half * sizeof(double));
        if (!re || !im) {
            free(re);
            free(im);
            return;
        }
        for (size_t k = 0; k < half; k++) {
            re[k] = data[2 * k];
            im[k] = data[2 * k + 1];
        }

        /* DC / Nyquist from even-odd FFT (C[0]). */
        double dc = re[0] + im[0];
        double nyq = re[0] - im[0];
        data[0] = dc;
        data[1] = nyq;

        for (size_t k = 1; k < quarter; k++) {
            size_t rk = half - k;
            double even_re = 0.5 * (re[k] + re[rk]);
            double even_im = 0.5 * (im[k] - im[rk]);
            double odd_re = 0.5 * (im[k] + im[rk]);
            double odd_im = -0.5 * (re[k] - re[rk]);
            double ang = -2.0 * M_PI * (double)k / (double)n;
            double wr = cos(ang), wi = sin(ang);
            double tw_re = odd_re * wr - odd_im * wi;
            double tw_im = odd_re * wi + odd_im * wr;
            data[2 * k] = even_re + tw_re;
            data[2 * k + 1] = even_im + tw_im;
            data[2 * rk] = even_re - tw_re;
            data[2 * rk + 1] = -(even_im - tw_im);
        }
        if (quarter > 0) {
            /* k = n/4: angle -π/2, odd branch only. */
            double even_re = re[quarter];
            double odd_im = -im[quarter];
            data[2 * quarter] = even_re;
            data[2 * quarter + 1] = odd_im;
        }
        free(re);
        free(im);
    } else {
        /* Unpack packed spectrum → length-half complex, inverse FFT. */
        double *re = (double *)malloc(half * sizeof(double));
        double *im = (double *)malloc(half * sizeof(double));
        if (!re || !im) {
            free(re);
            free(im);
            return;
        }
        double dc = data[0];
        double nyq = data[1];
        re[0] = 0.5 * (dc + nyq);
        im[0] = 0.5 * (dc - nyq);

        for (size_t k = 1; k < quarter; k++) {
            size_t rk = half - k;
            double fk_re = data[2 * k];
            double fk_im = data[2 * k + 1];
            double fr_re = data[2 * rk];
            double fr_im = data[2 * rk + 1];
            double even_re = 0.5 * (fk_re + fr_re);
            double even_im = 0.5 * (fk_im - fr_im);
            double ang = 2.0 * M_PI * (double)k / (double)n;
            double wr = cos(ang), wi = sin(ang);
            double diff_re = 0.5 * (fk_re - fr_re);
            double diff_im = 0.5 * (fk_im + fr_im);
            /* Undo forward twiddle on odd part. */
            double odd_re = diff_re * wr - diff_im * wi;
            double odd_im = diff_re * wi + diff_im * wr;
            /* Reconstruct C[k] = even + i*odd  (matching forward split). */
            re[k] = even_re - odd_im;
            im[k] = even_im + odd_re;
            re[rk] = even_re + odd_im;
            im[rk] = -even_im + odd_re;
        }
        if (quarter > 0) {
            re[quarter] = data[2 * quarter];
            im[quarter] = -data[2 * quarter + 1];
        }

        for (size_t k = 0; k < half; k++) {
            data[2 * k] = re[k];
            data[2 * k + 1] = im[k];
        }
        free(re);
        free(im);
        sqnr_four1(data, half, -1);
    }
}

/* Modified Bessel I0 from its power series / asymptotic — not NR bessi0. */
static double modified_bessel_i0(double x) {
    double ax = fabs(x);
    if (ax < 15.0) {
        /* I0(x) = Σ ((x/2)^{2k} / (k!)^2) */
        double sum = 1.0;
        double term = 1.0;
        double half = ax * 0.5;
        for (int k = 1; k < 64; k++) {
            term *= (half / (double)k) * (half / (double)k);
            sum += term;
            if (term < sum * 1e-18) break;
        }
        return sum;
    }
    /* Large-|x|: I0(x) ~ e^{|x|} / sqrt(2 π |x|) * (1 + 1/(8|x|) + …) */
    double inv = 1.0 / ax;
    double series = 1.0 + inv * (0.125 + inv * (0.0703125 + inv * 0.0732421875));
    return exp(ax) * series / sqrt(2.0 * M_PI * ax);
}

void sqnr_window(double *w, size_t n, SqnrWindow kind, double kaiser_beta) {
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        double t = (n == 1) ? 0.0 : (2.0 * (double)i / (double)(n - 1) - 1.0);
        double x = (double)i;
        switch (kind) {
        case SQNR_WIN_RECT:
            w[i] = 1.0;
            break;
        case SQNR_WIN_HANN:
            w[i] = 0.5 - 0.5 * cos(2.0 * M_PI * x / (double)(n - 1));
            break;
        case SQNR_WIN_HAMMING:
            w[i] = 0.54 - 0.46 * cos(2.0 * M_PI * x / (double)(n - 1));
            break;
        case SQNR_WIN_BLACKMAN:
            w[i] = 0.42 - 0.5 * cos(2.0 * M_PI * x / (double)(n - 1)) +
                   0.08 * cos(4.0 * M_PI * x / (double)(n - 1));
            break;
        case SQNR_WIN_KAISER: {
            double beta = kaiser_beta > 0 ? kaiser_beta : 8.6;
            double a = modified_bessel_i0(beta);
            w[i] = modified_bessel_i0(beta * sqrt(1.0 - t * t)) / a;
            break;
        }
        case SQNR_WIN_FLATTOP:
            w[i] = 0.21557895 - 0.41663158 * cos(2.0 * M_PI * x / (double)(n - 1)) +
                   0.277263158 * cos(4.0 * M_PI * x / (double)(n - 1)) -
                   0.083578947 * cos(6.0 * M_PI * x / (double)(n - 1)) +
                   0.006947368 * cos(8.0 * M_PI * x / (double)(n - 1));
            break;
        default:
            w[i] = 1.0;
            break;
        }
    }
}

void sqnr_apply_window(double *x, const double *w, size_t n) {
    for (size_t i = 0; i < n; i++) x[i] *= w[i];
}

size_t sqnr_magnitude_spectrum(const double *x, size_t n, double *mag_out,
                               double *scratch) {
    if (!sqnr_is_pow2(n)) return 0;
    memcpy(scratch, x, n * sizeof(double));
    sqnr_realft(scratch, n, 1);
    mag_out[0] = fabs(scratch[0]);
    mag_out[n / 2] = fabs(scratch[1]);
    for (size_t k = 1; k < n / 2; k++) {
        double re = scratch[2 * k];
        double im = scratch[2 * k + 1];
        mag_out[k] = sqrt(re * re + im * im);
    }
    return n / 2 + 1;
}

static double window_power(const double *w, size_t n) {
    double u = 0.0;
    for (size_t i = 0; i < n; i++) u += w[i] * w[i];
    return u;
}

size_t sqnr_periodogram(const double *x, size_t n, double fs, SqnrWindow win,
                        double *psd_out, double *freq_out, double *scratch) {
    if (!sqnr_is_pow2(n) || fs <= 0.0) return 0;
    double *w = scratch;
    double *buf = scratch + n;
    sqnr_window(w, n, win, 8.6);
    memcpy(buf, x, n * sizeof(double));
    sqnr_apply_window(buf, w, n);
    double U = window_power(w, n);
    sqnr_realft(buf, n, 1);
    size_t nb = n / 2 + 1;
    double scale = 1.0 / (fs * U);
    psd_out[0] = (buf[0] * buf[0]) * scale;
    psd_out[n / 2] = (buf[1] * buf[1]) * scale;
    freq_out[0] = 0.0;
    freq_out[n / 2] = fs * 0.5;
    for (size_t k = 1; k < n / 2; k++) {
        double re = buf[2 * k], im = buf[2 * k + 1];
        psd_out[k] = 2.0 * (re * re + im * im) * scale;
        freq_out[k] = fs * (double)k / (double)n;
    }
    return nb;
}

size_t sqnr_welch_psd(const double *x, size_t n, double fs, size_t seg,
                      double overlap, SqnrWindow win, double *psd_out,
                      double *freq_out, double *scratch) {
    if (!sqnr_is_pow2(seg) || seg > n || fs <= 0.0) return 0;
    if (overlap < 0.0) overlap = 0.0;
    if (overlap > 0.95) overlap = 0.95;
    size_t hop = (size_t)((1.0 - overlap) * (double)seg);
    if (hop < 1) hop = 1;
    size_t nb = seg / 2 + 1;
    for (size_t i = 0; i < nb; i++) psd_out[i] = 0.0;
    double *seg_psd = scratch;
    double *seg_freq = scratch + nb;
    double *inner = scratch + 2 * nb;
    size_t count = 0;
    for (size_t start = 0; start + seg <= n; start += hop) {
        sqnr_periodogram(x + start, seg, fs, win, seg_psd, seg_freq, inner);
        for (size_t i = 0; i < nb; i++) psd_out[i] += seg_psd[i];
        count++;
    }
    if (count == 0) return 0;
    for (size_t i = 0; i < nb; i++) {
        psd_out[i] /= (double)count;
        freq_out[i] = seg_freq[i];
    }
    return nb;
}

static size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void sqnr_correlate(const double *x, size_t n, double *out, double *scratch) {
    size_t np2 = next_pow2(n) << 1;
    double *a = scratch;
    memset(a, 0, np2 * sizeof(double));
    for (size_t i = 0; i < n; i++) a[i] = x[i];
    sqnr_realft(a, np2, 1);
    /* Power spectrum then inverse — autocorrelation. */
    a[0] = a[0] * a[0];
    a[1] = a[1] * a[1];
    for (size_t k = 1; k < np2 / 2; k++) {
        double re = a[2 * k], im = a[2 * k + 1];
        a[2 * k] = re * re + im * im;
        a[2 * k + 1] = 0.0;
    }
    sqnr_realft(a, np2, -1);
    double norm = 1.0 / (double)np2;
    for (size_t i = 0; i < n; i++) out[i] = a[i] * norm;
}

double sqnr_parabolic_bin(const double *mag, size_t nbins, size_t k) {
    if (k == 0 || k + 1 >= nbins) return (double)k;
    double alpha = mag[k - 1], beta = mag[k], gamma = mag[k + 1];
    double denom = alpha - 2.0 * beta + gamma;
    if (fabs(denom) < 1e-18) return (double)k;
    return (double)k + 0.5 * (alpha - gamma) / denom;
}

double sqnr_peak_frequency(const double *mag, const double *freq, size_t nbins) {
    if (nbins < 3) return 0.0;
    size_t kmax = 1;
    for (size_t k = 2; k < nbins - 1; k++)
        if (mag[k] > mag[kmax]) kmax = k;
    double bin = sqnr_parabolic_bin(mag, nbins, kmax);
    /* linear interpolate frequency axis */
    size_t i0 = (size_t)bin;
    if (i0 + 1 >= nbins) return freq[nbins - 1];
    double frac = bin - (double)i0;
    return freq[i0] * (1.0 - frac) + freq[i0 + 1] * frac;
}

double sqnr_pitch_autocorr(const double *x, size_t n, double fs, double fmin,
                           double fmax, double *scratch) {
    if (fmin <= 0 || fmax <= fmin || fs <= 0) return 0.0;
    double *ac = scratch;
    double *tmp = scratch + n;
    sqnr_correlate(x, n, ac, tmp);
    size_t lag_min = (size_t)(fs / fmax);
    size_t lag_max = (size_t)(fs / fmin);
    if (lag_max >= n) lag_max = n - 1;
    if (lag_min < 2) lag_min = 2;
    size_t best = lag_min;
    for (size_t lag = lag_min + 1; lag <= lag_max; lag++)
        if (ac[lag] > ac[best]) best = lag;
    if (best + 1 < n && best > 0) {
        double alpha = ac[best - 1], beta = ac[best], gamma = ac[best + 1];
        double denom = alpha - 2.0 * beta + gamma;
        if (fabs(denom) > 1e-18) {
            double delta = 0.5 * (alpha - gamma) / denom;
            return fs / ((double)best + delta);
        }
    }
    return fs / (double)best;
}

double sqnr_thd(const double *mag, const double *freq, size_t nbins, double fund_hz,
                int max_harmonic) {
    if (fund_hz <= 0 || nbins < 2 || max_harmonic < 2) return 0.0;
    double fund_p = 0.0, harm_p = 0.0;
    double df = freq[1] - freq[0];
    if (df <= 0) return 0.0;
    for (int h = 1; h <= max_harmonic; h++) {
        double target = fund_hz * (double)h;
        size_t k = (size_t)(target / df + 0.5);
        if (k >= nbins) break;
        size_t lo = k > 2 ? k - 2 : 1;
        size_t hi = k + 2 < nbins ? k + 2 : nbins - 1;
        double peak = 0.0;
        for (size_t i = lo; i <= hi; i++)
            if (mag[i] > peak) peak = mag[i];
        double p = peak * peak;
        if (h == 1) fund_p = p;
        else harm_p += p;
    }
    if (fund_p <= 0.0) return 0.0;
    return sqrt(harm_p / fund_p);
}

double sqnr_snr_db(const double *psd, const double *freq, size_t nbins, double fund_hz,
                   double bw_hz) {
    double sig = 0.0, noise = 0.0;
    for (size_t i = 1; i < nbins; i++) {
        if (fabs(freq[i] - fund_hz) <= bw_hz) sig += psd[i];
        else noise += psd[i];
    }
    if (noise <= 0.0) return 200.0;
    return 10.0 * log10(sig / noise);
}

int sqnr_linfit(const double *x, const double *y, size_t n, double *a, double *b) {
    if (n < 2) return -1;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; i++) {
        sx += x[i];
        sy += y[i];
        sxx += x[i] * x[i];
        sxy += x[i] * y[i];
    }
    double den = (double)n * sxx - sx * sx;
    if (fabs(den) < 1e-18) return -1;
    *b = ((double)n * sxy - sx * sy) / den;
    *a = (sy - (*b) * sx) / (double)n;
    return 0;
}

SqnrWindow sqnr_window_parse(const char *name) {
    if (!name) return SQNR_WIN_HANN;
    if (!strcmp(name, "rect") || !strcmp(name, "rectangular")) return SQNR_WIN_RECT;
    if (!strcmp(name, "hann") || !strcmp(name, "hanning")) return SQNR_WIN_HANN;
    if (!strcmp(name, "hamming")) return SQNR_WIN_HAMMING;
    if (!strcmp(name, "blackman")) return SQNR_WIN_BLACKMAN;
    if (!strcmp(name, "kaiser")) return SQNR_WIN_KAISER;
    if (!strcmp(name, "flattop")) return SQNR_WIN_FLATTOP;
    return SQNR_WIN_HANN;
}

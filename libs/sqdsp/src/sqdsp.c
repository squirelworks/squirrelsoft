#include "sqdsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int sqdsp_is_pow2(size_t n) { return n && !(n & (n - 1)); }

/*
 * Complex radix-2 FFT — original 0-based iterative Cooley–Tukey.
 * Layout: data[2*k]=Re, data[2*k+1]=Im for k = 0 .. nn-1.
 * isign > 0 → forward (e^{-2πi}), isign < 0 → inverse (no 1/nn scale).
 */
void sqdsp_fft(double *data, size_t nn, int isign) {
    if (nn < 2 || !sqdsp_is_pow2(nn)) return;

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
void sqdsp_rfft(double *data, size_t n, int isign) {
    if (n < 4 || !sqdsp_is_pow2(n)) return;
    const size_t half = n >> 1;
    const size_t quarter = n >> 2;

    if (isign >= 0) {
        /* Even/odd split → complex vector of length half, then FFT. */
        sqdsp_fft(data, half, 1);

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
        sqdsp_fft(data, half, -1);
    }
}

/* Modified Bessel I0 from its power series / asymptotic. */
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

void sqdsp_window(double *w, size_t n, SqdspWindow kind, double kaiser_beta) {
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        double t = (n == 1) ? 0.0 : (2.0 * (double)i / (double)(n - 1) - 1.0);
        double x = (double)i;
        switch (kind) {
        case SQDSP_WIN_RECT:
            w[i] = 1.0;
            break;
        case SQDSP_WIN_HANN:
            w[i] = 0.5 - 0.5 * cos(2.0 * M_PI * x / (double)(n - 1));
            break;
        case SQDSP_WIN_HAMMING:
            w[i] = 0.54 - 0.46 * cos(2.0 * M_PI * x / (double)(n - 1));
            break;
        case SQDSP_WIN_BLACKMAN:
            w[i] = 0.42 - 0.5 * cos(2.0 * M_PI * x / (double)(n - 1)) +
                   0.08 * cos(4.0 * M_PI * x / (double)(n - 1));
            break;
        case SQDSP_WIN_KAISER: {
            double beta = kaiser_beta > 0 ? kaiser_beta : 8.6;
            double a = modified_bessel_i0(beta);
            w[i] = modified_bessel_i0(beta * sqrt(1.0 - t * t)) / a;
            break;
        }
        case SQDSP_WIN_FLATTOP:
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

void sqdsp_apply_window(double *x, const double *w, size_t n) {
    for (size_t i = 0; i < n; i++) x[i] *= w[i];
}

size_t sqdsp_magnitude_spectrum(const double *x, size_t n, double *mag_out,
                               double *scratch) {
    if (!sqdsp_is_pow2(n)) return 0;
    memcpy(scratch, x, n * sizeof(double));
    sqdsp_rfft(scratch, n, 1);
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

size_t sqdsp_periodogram(const double *x, size_t n, double fs, SqdspWindow win,
                        double *psd_out, double *freq_out, double *scratch) {
    if (!sqdsp_is_pow2(n) || fs <= 0.0) return 0;
    double *w = scratch;
    double *buf = scratch + n;
    sqdsp_window(w, n, win, 8.6);
    memcpy(buf, x, n * sizeof(double));
    sqdsp_apply_window(buf, w, n);
    double U = window_power(w, n);
    sqdsp_rfft(buf, n, 1);
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

size_t sqdsp_welch_psd(const double *x, size_t n, double fs, size_t seg,
                      double overlap, SqdspWindow win, double *psd_out,
                      double *freq_out, double *scratch) {
    if (!sqdsp_is_pow2(seg) || seg > n || fs <= 0.0) return 0;
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
        sqdsp_periodogram(x + start, seg, fs, win, seg_psd, seg_freq, inner);
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

void sqdsp_correlate(const double *x, size_t n, double *out, double *scratch) {
    size_t np2 = next_pow2(n) << 1;
    double *a = scratch;
    memset(a, 0, np2 * sizeof(double));
    for (size_t i = 0; i < n; i++) a[i] = x[i];
    sqdsp_rfft(a, np2, 1);
    /* Power spectrum then inverse — autocorrelation. */
    a[0] = a[0] * a[0];
    a[1] = a[1] * a[1];
    for (size_t k = 1; k < np2 / 2; k++) {
        double re = a[2 * k], im = a[2 * k + 1];
        a[2 * k] = re * re + im * im;
        a[2 * k + 1] = 0.0;
    }
    sqdsp_rfft(a, np2, -1);
    double norm = 1.0 / (double)np2;
    for (size_t i = 0; i < n; i++) out[i] = a[i] * norm;
}

double sqdsp_parabolic_bin(const double *mag, size_t nbins, size_t k) {
    if (k == 0 || k + 1 >= nbins) return (double)k;
    double alpha = mag[k - 1], beta = mag[k], gamma = mag[k + 1];
    double denom = alpha - 2.0 * beta + gamma;
    if (fabs(denom) < 1e-18) return (double)k;
    return (double)k + 0.5 * (alpha - gamma) / denom;
}

size_t sqdsp_local_peaks(const double *spec, size_t nbins, double sample_rate,
                         size_t fft_size, size_t b0, size_t b1, double thresh_db,
                         size_t max_n, double min_sep_hz, double *hz, double *db) {
    size_t n = 0;
    if (!spec || !hz || !db || max_n == 0 || nbins < 3 || !(sample_rate > 0) || fft_size == 0)
        return 0;
    if (b0 < 1) b0 = 1;
    if (b1 >= nbins) b1 = nbins - 1;
    if (b0 > b1) return 0;
    for (size_t k = b0; k <= b1 && k + 1 < nbins; k++) {
        double v = spec[k];
        if (v < thresh_db || v < spec[k - 1] || v <= spec[k + 1]) continue;
        double bin = sqdsp_parabolic_bin(spec, nbins, k);
        double f = bin * sample_rate / (double)fft_size;
        /* keep the loudest of peaks closer than min_sep_hz */
        size_t j;
        for (j = 0; j < n; j++)
            if (fabs(hz[j] - f) < min_sep_hz) break;
        if (j < n) {
            if (v <= db[j]) continue;
            for (size_t m = j; m + 1 < n; m++) {
                hz[m] = hz[m + 1];
                db[m] = db[m + 1];
            }
            n--;
        }
        /* insert sorted by level, loudest first */
        size_t at = n;
        while (at > 0 && db[at - 1] < v) at--;
        if (at >= max_n) continue;
        size_t end = n < max_n ? n : max_n - 1;
        for (size_t m = end; m > at; m--) {
            hz[m] = hz[m - 1];
            db[m] = db[m - 1];
        }
        hz[at] = f;
        db[at] = v;
        if (n < max_n) n++;
    }
    return n;
}

double sqdsp_peak_frequency(const double *mag, const double *freq, size_t nbins) {
    if (nbins < 3) return 0.0;
    size_t kmax = 1;
    for (size_t k = 2; k < nbins - 1; k++)
        if (mag[k] > mag[kmax]) kmax = k;
    double bin = sqdsp_parabolic_bin(mag, nbins, kmax);
    /* linear interpolate frequency axis */
    size_t i0 = (size_t)bin;
    if (i0 + 1 >= nbins) return freq[nbins - 1];
    double frac = bin - (double)i0;
    return freq[i0] * (1.0 - frac) + freq[i0 + 1] * frac;
}

double sqdsp_pitch_autocorr(const double *x, size_t n, double fs, double fmin,
                           double fmax, double *scratch) {
    if (fmin <= 0 || fmax <= fmin || fs <= 0) return 0.0;
    double *ac = scratch;
    double *tmp = scratch + n;
    sqdsp_correlate(x, n, ac, tmp);
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

double sqdsp_thd(const double *mag, const double *freq, size_t nbins, double fund_hz,
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

double sqdsp_snr_db(const double *psd, const double *freq, size_t nbins, double fund_hz,
                   double bw_hz) {
    double sig = 0.0, noise = 0.0;
    for (size_t i = 1; i < nbins; i++) {
        if (fabs(freq[i] - fund_hz) <= bw_hz) sig += psd[i];
        else noise += psd[i];
    }
    if (noise <= 0.0) return 200.0;
    return 10.0 * log10(sig / noise);
}

int sqdsp_linfit(const double *x, const double *y, size_t n, double *a, double *b) {
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

SqdspWindow sqdsp_window_parse(const char *name) {
    if (!name) return SQDSP_WIN_HANN;
    if (!strcmp(name, "rect") || !strcmp(name, "rectangular")) return SQDSP_WIN_RECT;
    if (!strcmp(name, "hann") || !strcmp(name, "hanning")) return SQDSP_WIN_HANN;
    if (!strcmp(name, "hamming")) return SQDSP_WIN_HAMMING;
    if (!strcmp(name, "blackman")) return SQDSP_WIN_BLACKMAN;
    if (!strcmp(name, "kaiser")) return SQDSP_WIN_KAISER;
    if (!strcmp(name, "flattop")) return SQDSP_WIN_FLATTOP;
    return SQDSP_WIN_HANN;
}

double sqdsp_window_enbw(const double *w, size_t n) {
    double s = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) {
        s += w[i];
        s2 += w[i] * w[i];
    }
    return s != 0 ? (double)n * s2 / (s * s) : 1.0;
}

size_t sqdsp_octave_bands(int fraction, double fmin, double fmax, double *centre,
                          double *lo, double *hi, size_t cap) {
    if (fraction < 1 || !(fmax >= fmin) || fmax <= 0) return 0;
    double g = pow(10.0, 0.3);
    double b = (double)fraction;
    /* band index x for a frequency: fm = 1000 * g^(x/b) (odd b; even b
     * shifts by half a band, also per IEC 61260-1) */
    double shift = (fraction % 2) ? 0.0 : 0.5;
    double lo_f = fmin > 1e-3 ? fmin : 1e-3;
    long x0 = (long)ceil(b * log(lo_f / 1000.0) / log(g) - shift - 1e-9);
    long x1 = (long)floor(b * log(fmax / 1000.0) / log(g) - shift + 1e-9);
    size_t n = 0;
    for (long x = x0; x <= x1; x++, n++) {
        if (n >= cap) continue;
        double fm = 1000.0 * pow(g, ((double)x + shift) / b);
        double e = pow(g, 1.0 / (2.0 * b));
        if (centre) centre[n] = fm;
        if (lo) lo[n] = fm / e;
        if (hi) hi[n] = fm * e;
    }
    return n;
}

void sqdsp_band_levels_db(const double *mag_db, const double *freq, size_t nbins,
                          const double *lo, const double *hi, size_t nbands, double enbw,
                          double floor_db, double *out_db) {
    if (enbw <= 0) enbw = 1.0;
    for (size_t j = 0; j < nbands; j++) {
        double p = 0;
        int any = 0;
        for (size_t k = 0; k < nbins; k++) {
            if (freq[k] < lo[j] || freq[k] >= hi[j]) continue;
            p += pow(10.0, mag_db[k] / 10.0);
            any = 1;
        }
        double db = any && p > 0 ? 10.0 * log10(p / enbw) : floor_db;
        out_db[j] = db < floor_db ? floor_db : db;
    }
}

/* ---- multi-resolution spectrum ---- */

int sqdsp_multires_plan(SqdspMultires *p, size_t n, double fs, int levels, double q) {
    if (!p || !sqdsp_is_pow2(n) || n < 64 || !(fs > 0) || levels < 1 || !(q > 0)) return 0;
    if (levels > SQDSP_MR_MAX_LEVELS) levels = SQDSP_MR_MAX_LEVELS;
    memset(p, 0, sizeof *p);
    p->n = n;
    p->fs = fs;
    int k = 0;
    for (; k < levels; k++) {
        size_t m = n >> (2 * k);
        if (m < 64) break;
        p->size[k] = m;
        /* the shorter level's bins are fs / m wide: fine enough from q of them */
        p->cross_hz[k] = k ? q * fs / (double)m : 0.0;
    }
    p->levels = k;
    /* a crossover past Nyquist means that level never shows */
    while (p->levels > 1 && p->cross_hz[p->levels - 1] / sqrt(2.0) >= fs / 2) p->levels--;
    return p->levels;
}

/* dB of level spectrum lev (m points, as power) at frequency f, the power
 * linear between bins: so a band's power sum on the fine grid is the
 * level's own sum times the bins per level bin */
static double level_at(const double *lev, size_t m, double fs, double f) {
    double b = f * (double)m / fs;
    size_t nb = m / 2 + 1;
    double p;
    if (b <= 0) p = lev[0];
    else if (b >= (double)(nb - 1)) p = lev[nb - 1];
    else {
        size_t i = (size_t)b;
        double t = b - (double)i;
        p = lev[i] * (1 - t) + lev[i + 1] * t;
    }
    return 10.0 * log10(p + 1e-40);
}

size_t sqdsp_multires_db(const SqdspMultires *p, const double *x, SqdspWindow win, double *db_out,
                         double *work) {
    if (!p || p->levels < 1 || !x || !db_out || !work) return 0;
    size_t n = p->n, nb = n / 2 + 1;
    double *frame = work, *w = work + n, *mag = work + 2 * n;
    for (int k = p->levels - 1; k >= 0; k--) {
        /* coarsest first: it fills the whole grid, then each finer level
         * takes the band below the crossover with the level before it */
        size_t m = p->size[k], mb = m / 2 + 1;
        sqdsp_window(w, m, win, 8.6);
        double sum = 0;
        for (size_t i = 0; i < m; i++) {
            frame[i] = x[n - m + i] * w[i];
            sum += w[i];
        }
        if (sum <= 0) sum = (double)m;
        sqdsp_magnitude_spectrum(frame, m, mag, w); /* w is free again: scratch */
        for (size_t i = 0; i < mb; i++) {
            double scale = (i == 0 || i == m / 2) ? 1.0 / sum : 2.0 / sum;
            double a = mag[i] * scale;
            mag[i] = a * a;
        }
        if (k == p->levels - 1) {
            /* the coarsest: everywhere to begin with */
            for (size_t j = 0; j < nb; j++) db_out[j] = level_at(mag, m, p->fs, p->fs * (double)j / (double)n);
            continue;
        }
        /* this level is finer than everything so far: it owns the band below
         * the next level's crossover, fading out over the octave around it */
        double c = p->cross_hz[k + 1], lo = c / sqrt(2.0), hi = c * sqrt(2.0);
        for (size_t j = 0; j < nb; j++) {
            double f = p->fs * (double)j / (double)n;
            if (f >= hi) break;
            double v = level_at(mag, m, p->fs, f);
            if (f <= lo) db_out[j] = v;
            else {
                /* blend the power, so band sums stay true (sqdsp_multires_spread) */
                double t = log(f / lo) / log(hi / lo); /* 0 at lo, 1 at hi */
                db_out[j] = 10.0 * log10(pow(10.0, v / 10.0) * (1 - t) + pow(10.0, db_out[j] / 10.0) * t + 1e-40);
            }
        }
    }
    return nb;
}

double sqdsp_multires_spread(const SqdspMultires *p, double hz) {
    if (!p || p->levels < 1 || !p->size[0]) return 0;
    /* the same walk as sqdsp_multires_db, coarsest first */
    double s = (double)p->n / (double)p->size[p->levels - 1];
    for (int k = p->levels - 2; k >= 0; k--) {
        double c = p->cross_hz[k + 1], lo = c / sqrt(2.0), hi = c * sqrt(2.0);
        if (hz >= hi) break;
        double v = (double)p->n / (double)p->size[k];
        if (hz <= lo) s = v;
        else {
            double t = log(hz / lo) / log(hi / lo);
            s = v * (1 - t) + s * t;
        }
    }
    return s;
}

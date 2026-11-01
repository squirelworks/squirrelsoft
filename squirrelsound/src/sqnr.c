#include "sqnr.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int sqnr_is_pow2(size_t n) { return n && !(n & (n - 1)); }

void sqnr_four1(double *data, size_t nn, int isign) {
    /* Cooley–Tukey radix-2, iterative, bit-reversal permutation. */
    size_t n = nn << 1;
    size_t j = 1;
    for (size_t i = 1; i < n; i += 2) {
        if (j > i) {
            double tr = data[j - 1];
            double ti = data[j];
            data[j - 1] = data[i - 1];
            data[j] = data[i];
            data[i - 1] = tr;
            data[i] = ti;
        }
        size_t m = nn;
        while (m >= 2 && j > m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
    for (size_t mmax = 2; n > mmax; mmax <<= 1) {
        size_t istep = mmax << 1;
        double theta = isign * (2.0 * M_PI / (double)mmax);
        double wtemp = sin(0.5 * theta);
        double wpr = -2.0 * wtemp * wtemp;
        double wpi = sin(theta);
        double wr = 1.0;
        double wi = 0.0;
        for (size_t m = 1; m < mmax; m += 2) {
            for (size_t i = m; i <= n; i += istep) {
                j = i + mmax;
                double tempr = wr * data[j - 1] - wi * data[j];
                double tempi = wr * data[j] + wi * data[j - 1];
                data[j - 1] = data[i - 1] - tempr;
                data[j] = data[i] - tempi;
                data[i - 1] += tempr;
                data[i] += tempi;
            }
            wtemp = wr;
            wr = wr * wpr - wi * wpi + wr;
            wi = wi * wpr + wtemp * wpi + wi;
        }
    }
}

void sqnr_realft(double *data, size_t n, int isign) {
    /* Real FFT via complex half-length transform + packing identities. */
    size_t i, i1, i2, i3, i4;
    double c1 = 0.5, c2, h1r, h1i, h2r, h2i;
    double wr, wi, wpr, wpi, wtemp, theta;
    theta = M_PI / (double)(n >> 1);
    if (isign == 1) {
        c2 = -0.5;
        sqnr_four1(data, n >> 1, 1);
    } else {
        c2 = 0.5;
        theta = -theta;
    }
    wtemp = sin(0.5 * theta);
    wpr = -2.0 * wtemp * wtemp;
    wpi = sin(theta);
    wr = 1.0 + wpr;
    wi = wpi;
    for (i = 1; i < (n >> 2); i++) {
        i2 = 1 + (i1 = i + i);
        i4 = 1 + (i3 = n - i1);
        h1r = c1 * (data[i1] + data[i3]);
        h1i = c1 * (data[i2] - data[i4]);
        h2r = -c2 * (data[i2] + data[i4]);
        h2i = c2 * (data[i1] - data[i3]);
        data[i1] = h1r + wr * h2r - wi * h2i;
        data[i2] = h1i + wr * h2i + wi * h2r;
        data[i3] = h1r - wr * h2r + wi * h2i;
        data[i4] = -h1i + wr * h2i + wi * h2r;
        wtemp = wr;
        wr = wr * wpr - wi * wpi + wr;
        wi = wi * wpr + wtemp * wpi + wi;
    }
    if (isign == 1) {
        h1r = data[0];
        data[0] = h1r + data[1];
        data[1] = h1r - data[1];
    } else {
        h1r = data[0];
        data[0] = c1 * (h1r + data[1]);
        data[1] = c1 * (h1r - data[1]);
        sqnr_four1(data, n >> 1, -1);
    }
}

static double bessel_i0(double x) {
    double ax = fabs(x), y, ans;
    if (ax < 3.75) {
        y = x / 3.75;
        y *= y;
        ans = 1.0 + y * (3.5156229 +
                         y * (3.0899424 +
                              y * (1.2067492 +
                                   y * (0.2659732 + y * (0.0360768 + y * 0.0045813)))));
    } else {
        y = 3.75 / ax;
        ans = (exp(ax) / sqrt(ax)) *
              (0.39894228 +
               y * (0.01328592 +
                    y * (0.00225319 +
                         y * (-0.00157565 +
                              y * (0.00916281 +
                                   y * (-0.02057706 +
                                        y * (0.02635537 +
                                             y * (-0.01647633 + y * 0.00392377))))))));
    }
    return ans;
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
            double a = bessel_i0(beta);
            w[i] = bessel_i0(beta * sqrt(1.0 - t * t)) / a;
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

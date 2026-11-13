#include "live_engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int live_engine_init(LiveEngine *e, const LiveEngineConfig *cfg) {
    memset(e, 0, sizeof(*e));
    if (!cfg || !sqdsp_is_pow2(cfg->fft_size) || cfg->fft_size > LIVE_MAX_FFT ||
        cfg->fft_size < 256)
        return -1;
    e->cfg = *cfg;
    if (e->cfg.db_max <= e->cfg.db_min) {
        e->cfg.db_min = LIVE_DB_DEFAULT_MIN;
        e->cfg.db_max = LIVE_DB_DEFAULT_MAX;
    }
    if (e->cfg.f_max_hz <= 0) e->cfg.f_max_hz = cfg->sample_rate * 0.5;
    e->bins = cfg->fft_size / 2 + 1;
    e->wf_rows = LIVE_WATERFALL_ROWS;
    e->window = (double *)malloc(cfg->fft_size * sizeof(double));
    e->frame = (double *)malloc(cfg->fft_size * sizeof(double));
    e->scratch = (double *)malloc(cfg->fft_size * sizeof(double));
    e->mag = (double *)calloc(e->bins, sizeof(double));
    e->mag_hold = (double *)calloc(e->bins, sizeof(double));
    e->freq = (double *)malloc(e->bins * sizeof(double));
    e->time = (double *)calloc(cfg->fft_size, sizeof(double));
    e->waterfall = (float *)calloc(e->wf_rows * e->bins, sizeof(float));
    e->mr_work = (double *)malloc(3 * cfg->fft_size * sizeof(double));
    if (!e->window || !e->frame || !e->scratch || !e->mag || !e->mag_hold || !e->freq ||
        !e->time || !e->waterfall || !e->mr_work) {
        live_engine_free(e);
        return -1;
    }
    sqdsp_window(e->window, cfg->fft_size, cfg->window, 8.6);
    e->window_sum = 0.0;
    for (size_t i = 0; i < cfg->fft_size; i++) e->window_sum += e->window[i];
    if (e->window_sum <= 0.0) e->window_sum = (double)cfg->fft_size;
    for (size_t i = 0; i < e->wf_rows * e->bins; i++) e->waterfall[i] = (float)LIVE_DB_FLOOR;
    for (size_t k = 0; k < e->bins; k++)
        e->freq[k] = cfg->sample_rate * (double)k / (double)cfg->fft_size;
    live_engine_reset_hold(e);
    live_engine_set_multires(e, cfg->multires);
    return 0;
}

int live_engine_set_multires(LiveEngine *e, int levels) {
    if (!e || !e->mr_work) return 0;
    int got = levels > 1 ? sqdsp_multires_plan(&e->mr, e->cfg.fft_size, e->cfg.sample_rate, levels, 32.0) : 0;
    e->cfg.multires = got > 1 ? got : 0;
    live_engine_reset_hold(e);
    return got > 1 ? got : 1;
}

const float *live_engine_wf_row(const LiveEngine *e, size_t r) {
    return e->waterfall + ((e->wf_head + r) % e->wf_rows) * e->bins;
}

void live_engine_free(LiveEngine *e) {
    if (!e) return;
    free(e->window);
    free(e->frame);
    free(e->scratch);
    free(e->mag);
    free(e->mag_hold);
    free(e->freq);
    free(e->time);
    free(e->waterfall);
    free(e->mr_work);
    memset(e, 0, sizeof(*e));
}

void live_engine_reset_hold(LiveEngine *e) {
    if (!e || !e->mag_hold) return;
    for (size_t i = 0; i < e->bins; i++) e->mag_hold[i] = -1e300;
}

void live_engine_set_freq_range(LiveEngine *e, double fmin, double fmax) {
    if (!e) return;
    if (fmin < 0) fmin = 0;
    if (fmax > e->cfg.sample_rate * 0.5) fmax = e->cfg.sample_rate * 0.5;
    if (fmax <= fmin) return;
    e->cfg.f_min_hz = fmin;
    e->cfg.f_max_hz = fmax;
}

size_t live_engine_hz_to_bin(const LiveEngine *e, double hz) {
    if (!e || e->cfg.sample_rate <= 0) return 0;
    double b = hz * (double)e->cfg.fft_size / e->cfg.sample_rate;
    if (b < 0) b = 0;
    if (b > (double)(e->bins - 1)) b = (double)(e->bins - 1);
    return (size_t)(b + 0.5);
}

int live_engine_process(LiveEngine *e, const double *samples) {
    if (!e || !samples) return -1;
    memcpy(e->frame, samples, e->cfg.fft_size * sizeof(double));
    memcpy(e->time, samples, e->cfg.fft_size * sizeof(double));
    int mr = e->cfg.multires > 1;
    if (mr) {
        /* already dBFS, stitched from the levels */
        if (sqdsp_multires_db(&e->mr, samples, e->cfg.window, e->mag, e->mr_work) != e->bins) return -1;
    } else {
        sqdsp_apply_window(e->frame, e->window, e->cfg.fft_size);
        size_t nb = sqdsp_magnitude_spectrum(e->frame, e->cfg.fft_size, e->mag, e->scratch);
        if (nb != e->bins) return -1;
    }

    /* dBFS: one-sided bins scale by 2/sum(w), DC and Nyquist by 1/sum(w), so a
     * full-scale sine reads 0 dBFS whatever the window. */
    double half = (double)e->cfg.fft_size / 2.0;
    size_t peak_i = 1;
    double peak_db = -1e300;
    for (size_t k = 0; k < e->bins; k++) {
        double scale = (k == 0 || (double)k == half) ? 1.0 / e->window_sum
                                                     : 2.0 / e->window_sum;
        double db = (mr ? e->mag[k] : 20.0 * log10(e->mag[k] * scale + 1e-20)) + e->cfg.cal_db;
        if (db < LIVE_DB_FLOOR) db = LIVE_DB_FLOOR;
        e->mag[k] = db;
        if (e->cfg.max_hold) {
            if (db > e->mag_hold[k]) e->mag_hold[k] = db;
        }
        if (k > 0 && db > peak_db) {
            peak_db = db;
            peak_i = k;
        }
    }
    e->peak_db = peak_db;
    e->peak_hz = e->freq[peak_i];
    if (peak_i > 0 && peak_i + 1 < e->bins) {
        double bin = sqdsp_parabolic_bin(e->mag, e->bins, peak_i);
        size_t i0 = (size_t)bin;
        double frac = bin - (double)i0;
        if (i0 + 1 < e->bins)
            e->peak_hz = e->freq[i0] * (1.0 - frac) + e->freq[i0 + 1] * frac;
    }

    /* the newest row goes in front of the ring: nothing moves */
    e->wf_head = (e->wf_head + e->wf_rows - 1) % e->wf_rows;
    float *row = e->waterfall + e->wf_head * e->bins;
    for (size_t k = 0; k < e->bins; k++) row[k] = (float)e->mag[k];
    e->frames++;
    return 0;
}

LiveDirection live_direction_parse(const char *s, LiveDirection fallback) {
    if (!s) return fallback;
    if (strcmp(s, "waterfall") == 0) return LIVE_DIR_WATERFALL;
    if (strcmp(s, "fountain") == 0) return LIVE_DIR_FOUNTAIN;
    return fallback;
}

const char *live_direction_name(LiveDirection d) {
    return d == LIVE_DIR_FOUNTAIN ? "fountain" : "waterfall";
}

size_t live_waterfall_row_for_y(int y, int h, size_t rows, LiveDirection d) {
    if (rows == 0 || h <= 0) return 0;
    if (y < 0) y = 0;
    if (y >= h) y = h - 1;
    /* Distance from the newest edge, in screen rows. */
    int from_newest = (d == LIVE_DIR_FOUNTAIN) ? (h - 1 - y) : y;
    size_t row = (size_t)((double)from_newest / (double)h * (double)rows);
    return row >= rows ? rows - 1 : row;
}

unsigned int live_color_argb(float t) {
    static const unsigned char stops[7][3] = {
        {0, 0, 0}, {0, 0, 200}, {0, 190, 255}, {0, 210, 60},
        {255, 230, 0}, {255, 30, 0}, {255, 255, 255},
    };
    if (!(t > 0.0f)) t = 0.0f; /* also catches NaN */
    if (t > 1.0f) t = 1.0f;
    float x = t * 6.0f;
    int i = (int)x;
    if (i > 5) i = 5;
    float f = x - (float)i;
    unsigned int c = 0xFF000000u;
    for (int ch = 0; ch < 3; ch++) {
        float v = stops[i][ch] + (stops[i + 1][ch] - stops[i][ch]) * f;
        c |= (unsigned int)(v + 0.5f) << (16 - 8 * ch);
    }
    return c;
}

size_t live_engine_bands(const LiveEngine *e, int fraction, double *centre, double *db,
                         size_t cap) {
    enum { MAXB = 64 };
    double c[MAXB], lo[MAXB], hi[MAXB], lv[MAXB];
    size_t n = sqdsp_octave_bands(fraction, e->cfg.f_min_hz, e->cfg.f_max_hz, c, lo, hi, MAXB);
    if (n > MAXB) n = MAXB;
    if (n > cap) n = cap;
    double enbw = sqdsp_window_enbw(e->window, e->cfg.fft_size);
    const double *mag = e->mag;
    if (e->cfg.multires > 1) {
        /* each bin counts for less where a coarser level spread it wider;
         * mr_work is only scratch between frames */
        double *w = e->mr_work;
        for (size_t k = 0; k < e->bins; k++) {
            double s = sqdsp_multires_spread(&e->mr, e->freq[k]);
            w[k] = e->mag[k] - (s > 0 ? 10.0 * log10(s) : 0.0);
        }
        mag = w;
    }
    sqdsp_band_levels_db(mag, e->freq, e->bins, lo, hi, n, enbw, LIVE_DB_FLOOR, lv);
    for (size_t i = 0; i < n; i++) {
        if (centre) centre[i] = c[i];
        if (db) db[i] = lv[i];
    }
    return n;
}

size_t live_engine_peaks(const LiveEngine *e, const double *spec, size_t max_n,
                         double min_sep_hz, double *hz, double *db) {
    size_t n = 0;
    if (!spec || max_n == 0) return 0;
    size_t b0 = live_engine_hz_to_bin(e, e->cfg.f_min_hz);
    size_t b1 = live_engine_hz_to_bin(e, e->cfg.f_max_hz);
    if (b0 < 1) b0 = 1;
    double thresh = e->cfg.db_min + 10.0;
    for (size_t k = b0; k <= b1 && k + 1 < e->bins; k++) {
        double v = spec[k];
        if (v < thresh || v < spec[k - 1] || v <= spec[k + 1]) continue;
        double bin = sqdsp_parabolic_bin(spec, e->bins, k);
        double f = bin * e->cfg.sample_rate / (double)e->cfg.fft_size;
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

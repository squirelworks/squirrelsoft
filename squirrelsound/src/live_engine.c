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
        e->cfg.db_min = -90;
        e->cfg.db_max = 0;
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
    e->waterfall = (float *)calloc(e->wf_rows * e->bins, sizeof(float));
    if (!e->window || !e->frame || !e->scratch || !e->mag || !e->mag_hold || !e->freq ||
        !e->waterfall) {
        live_engine_free(e);
        return -1;
    }
    sqdsp_window(e->window, cfg->fft_size, cfg->window, 8.6);
    for (size_t k = 0; k < e->bins; k++)
        e->freq[k] = cfg->sample_rate * (double)k / (double)cfg->fft_size;
    live_engine_reset_hold(e);
    return 0;
}

void live_engine_free(LiveEngine *e) {
    if (!e) return;
    free(e->window);
    free(e->frame);
    free(e->scratch);
    free(e->mag);
    free(e->mag_hold);
    free(e->freq);
    free(e->waterfall);
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
    sqdsp_apply_window(e->frame, e->window, e->cfg.fft_size);
    size_t nb = sqdsp_magnitude_spectrum(e->frame, e->cfg.fft_size, e->mag, e->scratch);
    if (nb != e->bins) return -1;

    /* Convert to dBFS-ish relative to fft size */
    double norm = (double)e->cfg.fft_size;
    size_t peak_i = 1;
    double peak_db = -1e300;
    for (size_t k = 0; k < e->bins; k++) {
        double db = 20.0 * log10(e->mag[k] / norm + 1e-20);
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

    /* Scroll waterfall down; insert newest at row 0 */
    memmove(e->waterfall + e->bins, e->waterfall,
            (e->wf_rows - 1) * e->bins * sizeof(float));
    for (size_t k = 0; k < e->bins; k++) e->waterfall[k] = (float)e->mag[k];
    return 0;
}

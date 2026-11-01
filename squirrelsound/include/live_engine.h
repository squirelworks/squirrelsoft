#ifndef LIVE_ENGINE_H
#define LIVE_ENGINE_H

#include "sqnr.h"

#include <stddef.h>

#define LIVE_MAX_FFT 8192
#define LIVE_WATERFALL_ROWS 240

typedef struct {
    size_t fft_size;
    SqnrWindow window;
    double sample_rate;
    int max_hold;
    double db_min; /* e.g. -90 */
    double db_max; /* e.g. 0 */
    double f_min_hz;
    double f_max_hz;
} LiveEngineConfig;

typedef struct {
    LiveEngineConfig cfg;
    double *window;
    double *frame;
    double *scratch;
    double *mag;
    double *mag_hold;
    double *freq;
    float *waterfall; /* rows * bins, newest at row 0 after scroll */
    size_t bins;
    size_t wf_rows;
    double peak_hz;
    double peak_db;
} LiveEngine;

int live_engine_init(LiveEngine *e, const LiveEngineConfig *cfg);
void live_engine_free(LiveEngine *e);

/* Process mono frame of cfg.fft_size samples; updates mag, hold, waterfall. */
int live_engine_process(LiveEngine *e, const double *samples);

void live_engine_reset_hold(LiveEngine *e);
void live_engine_set_freq_range(LiveEngine *e, double fmin, double fmax);

/* Map frequency to bin index in current FFT. */
size_t live_engine_hz_to_bin(const LiveEngine *e, double hz);

#endif

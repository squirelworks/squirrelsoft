#ifndef LIVE_ENGINE_H
#define LIVE_ENGINE_H

#include "sqdsp.h"

#include <stddef.h>

#define LIVE_MAX_FFT 8192
#define LIVE_WATERFALL_ROWS 240

typedef struct {
    size_t fft_size;
    SqdspWindow window;
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

/* Waterfall: newest row at the top, history scrolls down.
 * Fountain:  newest row at the bottom, history scrolls up. */
typedef enum { LIVE_DIR_WATERFALL = 0, LIVE_DIR_FOUNTAIN = 1 } LiveDirection;

/* "waterfall" / "fountain" (case-sensitive); anything else returns fallback. */
LiveDirection live_direction_parse(const char *s, LiveDirection fallback);
const char *live_direction_name(LiveDirection d);

/* Which stored waterfall row (0 = newest) to draw at screen row y of a
 * height-h waterfall area, for the given direction. */
size_t live_waterfall_row_for_y(int y, int h, size_t rows, LiveDirection d);

#endif

#ifndef LIVE_ENGINE_H
#define LIVE_ENGINE_H

#include "sqdsp.h"

#include <stddef.h>

#define LIVE_MAX_FFT 8192
#define LIVE_WATERFALL_ROWS 240

/* dBFS: analysis floor and default display range (matches the Android app). */
#define LIVE_DB_FLOOR (-140.0)
#define LIVE_DB_DEFAULT_MIN (-120.0)
#define LIVE_DB_DEFAULT_MAX (-20.0)

typedef struct {
    size_t fft_size;
    SqdspWindow window;
    double sample_rate;
    int max_hold;
    double db_min; /* e.g. -90 */
    double db_max; /* e.g. 0 */
    double f_min_hz;
    double f_max_hz;
    double cal_db;  /* calibration offset added to every level (per device) */
    int multires;   /* 0 or 1: one FFT; 2 to 4: that many resolutions (sqdsp_multires) */
} LiveEngineConfig;

typedef struct {
    LiveEngineConfig cfg;
    double *window;
    double *frame;
    double *scratch;
    double *mag;
    double *mag_hold;
    double *freq;
    double *time;  /* last frame's samples, before the window (scope view) */
    float *waterfall; /* rows * bins, a ring: read it with live_engine_wf_row */
    size_t wf_head;   /* where the newest row is */
    unsigned long frames; /* frames processed, so a viewer knows what is new */
    SqdspMultires mr; /* the levels in use when cfg.multires > 1 */
    double *mr_work;
    double window_sum; /* sum of window coefficients, for dBFS scaling */
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
/* Row r of the waterfall, 0 the newest, wf_rows - 1 the oldest */
const float *live_engine_wf_row(const LiveEngine *e, size_t r);
/* Switch the multi-resolution spectrum (levels 2 to 4) or back to one FFT
 * (0 or 1). The hold is reset: levels read differently in the noise.
 * Returns the levels in use (1 for one FFT). */
int live_engine_set_multires(LiveEngine *e, int levels);
void live_engine_set_freq_range(LiveEngine *e, double fmin, double fmax);

/* Opaque 0xAARRGGBB colour for t in 0..1: black > blue > cyan > green >
 * yellow > red > white, black at the floor. Out-of-range t is clamped. */
unsigned int live_color_argb(float t);

/* Fractional-octave band levels of the current spectrum (fraction 1 or 3),
 * for bands centred inside the frequency range. Writes up to cap centres
 * and levels and returns the band count. Includes cal_db like mag. */
size_t live_engine_bands(const LiveEngine *e, int fraction, double *centre, double *db,
                         size_t cap);

/* Up to max_n peaks of spec (e->mag or e->mag_hold) inside the frequency
 * range, loudest first: local maxima at least 10 dB above db_min and
 * min_sep_hz apart. Frequencies are interpolated. Returns the count. */
size_t live_engine_peaks(const LiveEngine *e, const double *spec, size_t max_n,
                         double min_sep_hz, double *hz, double *db);

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

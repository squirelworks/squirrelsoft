/* SquirrelSound Live window on libs/sqgui: toolbar, spectrum, LOFARgram.
 * Original code. */
#include "live_gui.h"

#include <stdio.h>
#include <string.h>

#define COL_BG      0xFF0A1018u
#define COL_GRID    0xFF1E2A36u
#define COL_AXIS    0xFF8090A0u
#define COL_TRACE   0xFF50DCFFu
#define COL_HOLD    0xFFFFB43Cu
#define COL_PEAK    0xFFFF5078u
#define COL_BAR     0xFF3CA0E0u
#define COL_SCOPE   0xFF60FF90u
#define COL_LABEL   0xFFE8E8E8u

static const char *const range_names[LIVE_RANGE_COUNT] = { "Human", "Music", "Bass", "Speech",
                                                            "Full" };
static const char *const speed_names[3] = { "Slow", "Normal", "Fast" };
static const char *const view_names[LIVE_VIEW_COUNT] = { "Spectrum", "Octave", "1/3 Oct",
                                                         "Scope" };

void live_gui_init(LiveGui *g, LiveEngine *e, LiveDirection dir, const char *source)
{
    memset(g, 0, sizeof *g);
    g->speed = LIVE_SPEED_FAST;
    g->fountain = dir == LIVE_DIR_FOUNTAIN;
    g->source = source ? source : "";
    g->peaks = 1;
    live_gui_set_range(g, e, LIVE_RANGE_HUMAN);
}

void live_gui_reset_hold(LiveGui *g, LiveEngine *e)
{
    live_engine_reset_hold(e);
    g->band_n = 0;
}

void live_gui_set_cal(LiveGui *g, LiveEngine *e, double db)
{
    if (db < -100)
        db = -100;
    if (db > 100)
        db = 100;
    if (db == e->cfg.cal_db)
        return;
    double d = db - e->cfg.cal_db;
    e->cfg.cal_db = db;
    /* shift what is on screen too, so the trace does not jump at the next frame */
    for (size_t k = 0; k < e->bins; k++) {
        e->mag[k] += d;
        if (e->mag_hold[k] > -1e299)
            e->mag_hold[k] += d;
    }
    e->peak_db += d;
    g->band_n = 0;
    g->cal_changed = 1;
}

LiveDirection live_gui_direction(const LiveGui *g)
{
    return g->fountain ? LIVE_DIR_FOUNTAIN : LIVE_DIR_WATERFALL;
}

int live_gui_scroll_skip(const LiveGui *g)
{
    return g->speed == LIVE_SPEED_SLOW ? 4 : g->speed == LIVE_SPEED_NORMAL ? 2 : 1;
}

int live_gui_take_frame(LiveGui *g)
{
    if (++g->scroll_phase < live_gui_scroll_skip(g))
        return 0;
    g->scroll_phase = 0;
    return 1;
}

void live_gui_set_range(LiveGui *g, LiveEngine *e, int range)
{
    double nyq = e->cfg.sample_rate * 0.5;
    double lo = 20, hi = nyq;
    switch (range) {
    case LIVE_RANGE_HUMAN: hi = nyq < 16000.0 ? nyq : 16000.0; break;
    case LIVE_RANGE_MUSIC: hi = 4000; break;
    case LIVE_RANGE_BASS: hi = 250; break;
    case LIVE_RANGE_SPEECH: lo = 100; hi = 8000; break;
    default: range = LIVE_RANGE_FULL; break;
    }
    live_engine_set_freq_range(e, lo, hi);
    g->range = range;
}

void live_gui_zoom(LiveGui *g, LiveEngine *e, double t, double factor)
{
    double lo = e->cfg.f_min_hz, hi = e->cfg.f_max_hz;
    double anchor = lo + t * (hi - lo);
    double span = (hi - lo) * factor;
    if (span < 40)
        span = 40;
    live_engine_set_freq_range(e, anchor - t * span, anchor + (1 - t) * span);
    g->range = -1;
}

static void pan(LiveGui *g, LiveEngine *e, double frac)
{
    double lo = e->cfg.f_min_hz, hi = e->cfg.f_max_hz, step = (hi - lo) * frac;
    live_engine_set_freq_range(e, lo + step, hi + step);
    g->range = -1;
}

int live_gui_key(LiveGui *g, LiveEngine *e, int ascii, int sqg_key)
{
    if (sqg_key == SQG_KEY_ESCAPE || ascii == 'q')
        return 1;
    if (sqg_key == SQG_KEY_LEFT)
        pan(g, e, -0.1);
    if (sqg_key == SQG_KEY_RIGHT)
        pan(g, e, 0.1);
    switch (ascii) {
    case ' ': g->paused = !g->paused; break;
    case 'm':
        e->cfg.max_hold = !e->cfg.max_hold;
        if (!e->cfg.max_hold)
            live_gui_reset_hold(g, e);
        break;
    case 'r': live_gui_reset_hold(g, e); break;
    case 'v': g->view = (g->view + 1) % LIVE_VIEW_COUNT; break;
    case 'p': g->peaks = !g->peaks; break;
    case 'R': g->record = !g->record; g->record_changed = 1; break;
    case '-': live_gui_set_cal(g, e, e->cfg.cal_db - 1); break;
    case '=': case '+': live_gui_set_cal(g, e, e->cfg.cal_db + 1); break;
    case '1': case '2': case '3': case '4': case '5':
        live_gui_set_range(g, e, ascii - '1');
        break;
    case '8': g->speed = LIVE_SPEED_SLOW; break;
    case '9': g->speed = LIVE_SPEED_NORMAL; break;
    case '0': g->speed = LIVE_SPEED_FAST; break;
    case 'w': g->fountain = !g->fountain; g->dir_changed = 1; break;
    case '[': live_gui_zoom(g, e, 0.5, 0.8); break;
    case ']': live_gui_zoom(g, e, 0.5, 1.25); break;
    default: break;
    }
    return 0;
}

static void fmt_hz(char *buf, size_t n, double hz)
{
    if (hz >= 10000)
        snprintf(buf, n, "%.0fk", hz / 1000);
    else if (hz >= 1000)
        snprintf(buf, n, "%.1fk", hz / 1000);
    else
        snprintf(buf, n, "%.0f", hz);
}

static void toolbar(LiveGui *g, sqg_ui *ui, LiveEngine *e, sqg_rect area)
{
    sqg_canvas *cv = ui->cv;
    int lh = sqg_line_h(ui);
    sqg_bevel(cv, area, 0, 1, ui->theme);
    sqg_rect r1 = sqg_rect_make(area.x + 4, area.y + 4, area.w - 8, lh);
    sqg_rect r2 = sqg_rect_make(area.x + 4, area.y + 8 + lh, area.w - 8, lh);

    sqg_row row = sqg_row_begin(r1, 4);
    const char *run = g->paused ? "Start" : "Stop";
    if (sqg_button(ui, sqg_id_str("run"), sqg_row_take(&row, sqg_button_w(ui, "Start")), run))
        g->paused = !g->paused;
    int hold = e->cfg.max_hold;
    if (sqg_toggle(ui, sqg_id_str("hold"), sqg_row_take(&row, sqg_button_w(ui, "Hold")), "Hold",
                   &hold)) {
        e->cfg.max_hold = hold;
        if (!hold)
            live_gui_reset_hold(g, e);
    }
    if (sqg_button(ui, sqg_id_str("reset"), sqg_row_take(&row, sqg_button_w(ui, "Reset")),
                   "Reset"))
        live_gui_reset_hold(g, e);
    sqg_toggle(ui, sqg_id_str("peaks"), sqg_row_take(&row, sqg_button_w(ui, "Peaks")), "Peaks",
               &g->peaks);
    if (sqg_toggle(ui, sqg_id_str("fountain"),
                   sqg_row_take(&row, sqg_button_w(ui, "Fountain")), "Fountain", &g->fountain))
        g->dir_changed = 1;
    if (sqg_toggle(ui, sqg_id_str("rec"), sqg_row_take(&row, sqg_button_w(ui, "Rec")), "Rec",
                   &g->record))
        g->record_changed = 1;
    char lo[16], hi[16], cal[32] = "", status[300];
    fmt_hz(lo, sizeof lo, e->cfg.f_min_hz);
    fmt_hz(hi, sizeof hi, e->cfg.f_max_hz);
    if (e->cfg.cal_db != 0)
        snprintf(cal, sizeof cal, "  cal %+.1f dB", e->cfg.cal_db);
    snprintf(status, sizeof status, "%s%speak %.1f Hz %.1f dB%s  %s-%s Hz  %s%s",
             g->record && g->rec_info ? g->rec_info : "", g->record && g->rec_info ? "  " : "",
             e->peak_hz, e->peak_db, cal, lo, hi, g->source, g->paused ? "  (stopped)" : "");
    sqg_label(ui, sqg_row_take(&row, 0), status, SQG_ALIGN_RIGHT);

    sqg_row row2 = sqg_row_begin(r2, 8);
    int range = g->range;
    if (sqg_segments(ui, sqg_id_str("range"), sqg_row_take(&row2, r2.w * 2 / 5), range_names,
                     LIVE_RANGE_COUNT, &range))
        live_gui_set_range(g, e, range);
    sqg_segments(ui, sqg_id_str("view"), sqg_row_take(&row2, r2.w * 7 / 20), view_names,
                 LIVE_VIEW_COUNT, &g->view);
    sqg_segments(ui, sqg_id_str("speed"), sqg_row_take(&row2, 0), speed_names, 3, &g->speed);
}

static int db_to_y(double db, double dmin, double dmax, sqg_rect r)
{
    double span = dmax - dmin;
    if (span < 1)
        span = 1;
    int y = r.y + r.h - 1 - (int)((db - dmin) / span * (r.h - 1));
    if (y < r.y)
        y = r.y;
    if (y > r.y + r.h - 1)
        y = r.y + r.h - 1;
    return y;
}

static size_t bin_for_x(const LiveEngine *e, int x, sqg_rect r, size_t b0, size_t b1)
{
    size_t k = b0 + (size_t)((double)(x - r.x) / (double)r.w * (double)(b1 - b0));
    return k >= e->bins ? e->bins - 1 : k;
}

static void spectrum(sqg_ui *ui, LiveEngine *e, sqg_rect r, size_t b0, size_t b1)
{
    sqg_canvas *cv = ui->cv;
    double dmin = e->cfg.db_min, dmax = e->cfg.db_max;
    double flo = e->cfg.f_min_hz, fhi = e->cfg.f_max_hz;
    sqg_fill(cv, r, COL_BG);
    /* grid: every 20 dB, and five frequency ticks */
    for (int db = -140; db <= 0; db += 20) {
        if (db <= dmin || db >= dmax)
            continue;
        int y = db_to_y(db, dmin, dmax, r);
        sqg_hline(cv, r.x, r.x + r.w - 1, y, COL_GRID);
        char t[8];
        snprintf(t, sizeof t, "%d", db);
        sqg_text(cv, r.x + 2, y + 2, t, COL_AXIS, 1);
    }
    for (int i = 1; i < 5; i++) {
        int x = r.x + r.w * i / 5;
        sqg_vline(cv, x, r.y, r.y + r.h - 1, COL_GRID);
        char t[16];
        fmt_hz(t, sizeof t, flo + (fhi - flo) * i / 5);
        sqg_text(cv, x + 2, r.y + r.h - 10, t, COL_AXIS, 1);
    }
    sqg_rect old = sqg_clip(cv, r);
    int prev = -1;
    for (int x = r.x; x < r.x + r.w; x++) {
        size_t k = bin_for_x(e, x, r, b0, b1);
        int y = db_to_y(e->mag[k], dmin, dmax, r);
        if (prev >= 0)
            sqg_vline(cv, x, prev, y, COL_TRACE);
        else
            sqg_pixel(cv, x, y, COL_TRACE);
        prev = y;
        if (e->cfg.max_hold)
            sqg_pixel(cv, x, db_to_y(e->mag_hold[k], dmin, dmax, r), COL_HOLD);
    }
    if (e->peak_hz >= flo && e->peak_hz <= fhi && fhi > flo) {
        int px = r.x + (int)((e->peak_hz - flo) / (fhi - flo) * (r.w - 1));
        sqg_vline(cv, px, r.y, r.y + r.h - 1, COL_PEAK);
    }
    cv->clip = old;
}

/* Small triangles with "frequency level" labels on the loudest peaks */
static void peak_labels(sqg_ui *ui, LiveEngine *e, sqg_rect r)
{
    sqg_canvas *cv = ui->cv;
    double hz[LIVE_GUI_MAX_PEAKS], db[LIVE_GUI_MAX_PEAKS];
    double flo = e->cfg.f_min_hz, fhi = e->cfg.f_max_hz;
    if (fhi <= flo)
        return;
    const double *spec = e->cfg.max_hold ? e->mag_hold : e->mag;
    size_t n = live_engine_peaks(e, spec, LIVE_GUI_MAX_PEAKS, (fhi - flo) / 40, hz, db);
    sqg_rect old = sqg_clip(cv, r);
    sqg_rect placed[LIVE_GUI_MAX_PEAKS];
    int np = 0;
    for (size_t i = 0; i < n; i++) {
        int x = r.x + (int)((hz[i] - flo) / (fhi - flo) * (r.w - 1));
        int y = db_to_y(db[i], e->cfg.db_min, e->cfg.db_max, r) - 3;
        if (y < r.y + 4)
            y = r.y + 4;
        for (int k = 0; k < 4; k++)
            sqg_hline(cv, x - k, x + k, y - 3 + k, COL_PEAK);
        char f[16], t[40];
        fmt_hz(f, sizeof f, hz[i]);
        snprintf(t, sizeof t, "%s %.0f", f, db[i]);
        int tw = sqg_text_width(t, 1);
        int tx = x - tw / 2;
        if (tx < r.x + 2)
            tx = r.x + 2;
        if (tx + tw > r.x + r.w - 2)
            tx = r.x + r.w - 2 - tw;
        int ty = y - 13;
        if (ty < r.y + 2)
            ty = y + 4;
        /* loudest first: a quieter label steps down until it is clear */
        sqg_rect lr = sqg_rect_make(tx - 1, ty - 1, tw + 2, 10);
        for (int tries = 0; tries < 8; tries++) {
            int hit = 0;
            for (int j = 0; j < np; j++)
                if (sqg_rect_intersect(lr, placed[j]).w > 0 &&
                    sqg_rect_intersect(lr, placed[j]).h > 0)
                    hit = 1;
            if (!hit)
                break;
            lr.y += 10;
        }
        placed[np++] = lr;
        sqg_text(cv, lr.x + 1, lr.y + 1, t, COL_LABEL, 1);
    }
    cv->clip = old;
}

static void level_grid(sqg_canvas *cv, const LiveEngine *e, sqg_rect r)
{
    double dmin = e->cfg.db_min, dmax = e->cfg.db_max;
    for (int db = -140; db <= 40; db += 20) {
        if (db <= dmin || db >= dmax)
            continue;
        int y = db_to_y(db, dmin, dmax, r);
        sqg_hline(cv, r.x, r.x + r.w - 1, y, COL_GRID);
        char t[8];
        snprintf(t, sizeof t, "%d", db);
        sqg_text(cv, r.x + 2, y + 2, t, COL_AXIS, 1);
    }
}

/* Octave or third-octave bars, one equal-width bar per band */
static void bands(LiveGui *g, sqg_ui *ui, LiveEngine *e, sqg_rect r, int fraction)
{
    sqg_canvas *cv = ui->cv;
    double c[LIVE_GUI_MAX_BANDS], db[LIVE_GUI_MAX_BANDS];
    sqg_fill(cv, r, COL_BG);
    level_grid(cv, e, r);
    size_t n = live_engine_bands(e, fraction, c, db, LIVE_GUI_MAX_BANDS);
    if (n == 0) {
        sqg_text(cv, r.x + 40, r.y + r.h / 2, "no bands in this range", COL_AXIS, 1);
        return;
    }
    if ((int)n != g->band_n || fraction != g->band_frac) {
        g->band_n = (int)n;
        g->band_frac = fraction;
        for (size_t i = 0; i < n; i++)
            g->band_hold[i] = -1e300;
    }
    int left = 30, label_h = 12;
    sqg_rect plot = sqg_rect_make(r.x + left, r.y + 4, r.w - left - 4, r.h - 4 - label_h);
    if (plot.w < (int)n || plot.h < 8)
        return;
    sqg_rect old = sqg_clip(cv, r);
    int last_label = -1000;
    for (size_t i = 0; i < n; i++) {
        int x0 = plot.x + plot.w * (int)i / (int)n, x1 = plot.x + plot.w * ((int)i + 1) / (int)n;
        int gap = (x1 - x0) > 6 ? 2 : (x1 - x0) > 2 ? 1 : 0;
        int y = db_to_y(db[i], e->cfg.db_min, e->cfg.db_max, plot);
        sqg_fill(cv, sqg_rect_make(x0 + gap, y, x1 - x0 - 2 * gap, plot.y + plot.h - y), COL_BAR);
        if (e->cfg.max_hold) {
            if (db[i] > g->band_hold[i])
                g->band_hold[i] = db[i];
            int hy = db_to_y(g->band_hold[i], e->cfg.db_min, e->cfg.db_max, plot);
            sqg_hline(cv, x0 + gap, x1 - gap - 1, hy, COL_HOLD);
        }
        char t[16];
        fmt_hz(t, sizeof t, c[i]);
        int tw = sqg_text_width(t, 1), tx = (x0 + x1) / 2 - tw / 2;
        if (tx > last_label + 4) {
            sqg_text(cv, tx, plot.y + plot.h + 3, t, COL_AXIS, 1);
            last_label = tx + tw;
        }
    }
    cv->clip = old;
}

/* The last frame's samples, one min-max line per column, started on a
 * rising zero crossing so a steady tone stands still */
static void scope(sqg_ui *ui, LiveEngine *e, sqg_rect r)
{
    sqg_canvas *cv = ui->cv;
    sqg_fill(cv, r, COL_BG);
    int mid = r.y + r.h / 2;
    sqg_hline(cv, r.x, r.x + r.w - 1, mid, COL_GRID);
    sqg_hline(cv, r.x, r.x + r.w - 1, r.y + r.h / 4, COL_GRID);
    sqg_hline(cv, r.x, r.x + r.w - 1, r.y + 3 * r.h / 4, COL_GRID);
    size_t n = e->cfg.fft_size, start = 0, span = n / 2;
    for (size_t i = 1; i < n - span; i++)
        if (e->time[i - 1] < 0 && e->time[i] >= 0) {
            start = i;
            break;
        }
    char t[48];
    snprintf(t, sizeof t, "%.1f ms", 1000.0 * (double)span / e->cfg.sample_rate);
    sqg_text(cv, r.x + r.w - sqg_text_width(t, 1) - 4, r.y + 4, t, COL_AXIS, 1);
    sqg_text(cv, r.x + 2, r.y + 2, "+1", COL_AXIS, 1);
    sqg_text(cv, r.x + 2, r.y + r.h - 10, "-1", COL_AXIS, 1);
    sqg_rect old = sqg_clip(cv, r);
    int half = r.h / 2 - 1, prev = mid;
    for (int x = 0; x < r.w; x++) {
        size_t a = start + span * (size_t)x / (size_t)r.w;
        size_t b = start + span * (size_t)(x + 1) / (size_t)r.w;
        if (b <= a)
            b = a + 1;
        double lo = 1e9, hi = -1e9;
        for (size_t i = a; i < b && i < n; i++) {
            if (e->time[i] < lo)
                lo = e->time[i];
            if (e->time[i] > hi)
                hi = e->time[i];
        }
        int y0 = mid - (int)(hi * half), y1 = mid - (int)(lo * half);
        if (prev < y0)
            y0 = prev;
        if (prev > y1)
            y1 = prev;
        sqg_vline(cv, r.x + x, y0, y1, COL_SCOPE);
        prev = mid - (int)((lo + hi) / 2 * half);
    }
    cv->clip = old;
}

static void lofargram(sqg_ui *ui, LiveEngine *e, sqg_rect r, size_t b0, size_t b1,
                      LiveDirection dir)
{
    sqg_canvas *cv = ui->cv;
    float dmin = (float)e->cfg.db_min, span = (float)(e->cfg.db_max - e->cfg.db_min);
    if (span < 1)
        span = 1;
    sqg_rect c = sqg_rect_intersect(r, cv->clip);
    for (int y = c.y; y < c.y + c.h; y++) {
        size_t row = live_waterfall_row_for_y(y - r.y, r.h, e->wf_rows, dir);
        const float *src = e->waterfall + row * e->bins;
        uint32_t *dst = cv->px + (size_t)y * (size_t)cv->stride;
        for (int x = c.x; x < c.x + c.w; x++)
            dst[x] = live_color_argb((src[bin_for_x(e, x, r, b0, b1)] - dmin) / span);
    }
}

void live_gui_frame(LiveGui *g, sqg_ui *ui, LiveEngine *e)
{
    sqg_canvas *cv = ui->cv;
    int lh = sqg_line_h(ui);
    sqg_rect bar = sqg_rect_make(0, 0, cv->w, 2 * lh + 12);
    toolbar(g, ui, e, bar);

    int top = bar.h, h = cv->h - top;
    if (h < 2)
        return;
    g->spectrum = sqg_rect_make(0, top, cv->w, h / 2);
    g->waterfall = sqg_rect_make(0, top + h / 2, cv->w, h - h / 2);

    /* the wheel over the plots zooms around the pointer */
    sqg_rect plots = sqg_rect_make(0, top, cv->w, h);
    if (ui->in.wheel && sqg_rect_contains(plots, ui->in.mx, ui->in.my) && cv->w > 1)
        live_gui_zoom(g, e, (double)ui->in.mx / (cv->w - 1), ui->in.wheel > 0 ? 0.85 : 1.15);

    size_t b0 = live_engine_hz_to_bin(e, e->cfg.f_min_hz);
    size_t b1 = live_engine_hz_to_bin(e, e->cfg.f_max_hz);
    if (b1 <= b0)
        b1 = b0 + 1;
    switch (g->view) {
    case LIVE_VIEW_OCTAVE: bands(g, ui, e, g->spectrum, 1); break;
    case LIVE_VIEW_THIRD: bands(g, ui, e, g->spectrum, 3); break;
    case LIVE_VIEW_SCOPE: scope(ui, e, g->spectrum); break;
    default:
        spectrum(ui, e, g->spectrum, b0, b1);
        if (g->peaks)
            peak_labels(ui, e, g->spectrum);
        break;
    }
    lofargram(ui, e, g->waterfall, b0, b1, live_gui_direction(g));
    sqg_hline(cv, 0, cv->w - 1, g->waterfall.y, COL_AXIS);
}

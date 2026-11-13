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

static const char *const range_names[LIVE_RANGE_COUNT] = { "Human", "Music", "Bass", "Speech",
                                                            "Full" };
static const char *const speed_names[3] = { "Slow", "Normal", "Fast" };

void live_gui_init(LiveGui *g, LiveEngine *e, LiveDirection dir, const char *source)
{
    memset(g, 0, sizeof *g);
    g->speed = LIVE_SPEED_FAST;
    g->fountain = dir == LIVE_DIR_FOUNTAIN;
    g->source = source ? source : "";
    live_gui_set_range(g, e, LIVE_RANGE_HUMAN);
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
            live_engine_reset_hold(e);
        break;
    case 'r': live_engine_reset_hold(e); break;
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
            live_engine_reset_hold(e);
    }
    if (sqg_button(ui, sqg_id_str("reset"), sqg_row_take(&row, sqg_button_w(ui, "Reset")),
                   "Reset"))
        live_engine_reset_hold(e);
    if (sqg_toggle(ui, sqg_id_str("fountain"),
                   sqg_row_take(&row, sqg_button_w(ui, "Fountain")), "Fountain", &g->fountain))
        g->dir_changed = 1;
    char lo[16], hi[16], status[200];
    fmt_hz(lo, sizeof lo, e->cfg.f_min_hz);
    fmt_hz(hi, sizeof hi, e->cfg.f_max_hz);
    snprintf(status, sizeof status, "peak %.1f Hz %.1f dBFS  %s-%s Hz  %s%s", e->peak_hz,
             e->peak_db, lo, hi, g->source, g->paused ? "  (stopped)" : "");
    sqg_label(ui, sqg_row_take(&row, 0), status, SQG_ALIGN_RIGHT);

    sqg_row row2 = sqg_row_begin(r2, 8);
    int rw = r2.w * 3 / 5;
    int range = g->range;
    if (sqg_segments(ui, sqg_id_str("range"), sqg_row_take(&row2, rw), range_names,
                     LIVE_RANGE_COUNT, &range))
        live_gui_set_range(g, e, range);
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
    spectrum(ui, e, g->spectrum, b0, b1);
    lofargram(ui, e, g->waterfall, b0, b1, live_gui_direction(g));
    sqg_hline(cv, 0, cv->w - 1, g->waterfall.y, COL_AXIS);
}

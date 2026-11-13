/* SquirrelSound Live window (live_gui on libs/sqgui), drawn off screen.
 * Feeds a synthetic 1 kHz tone, then checks the spectrum trace, the peak
 * marker, the LOFARgram rows, the toolbar widgets and the shortcuts.
 * Writes the last frame to argv[1] as a PPM when given. */
#include "live_engine.h"
#include "live_gui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define W 800
#define H 500
#define COL_TRACE 0xFF50DCFFu
#define COL_PEAK 0xFFFF5078u

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line)
{
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}

static uint32_t px[W * H];
static sqg_canvas cv;
static sqg_ui ui;
static sqg_input in;
static LiveGui g;
static LiveEngine e;

static uint32_t get(int x, int y) { return px[y * W + x]; }

static void frame(void)
{
    sqg_begin(&ui, &cv, &in);
    live_gui_frame(&g, &ui, &e);
    sqg_end(&ui);
    in.pressed = in.released = 0;
    in.wheel = 0;
    in.key = 0;
    in.text[0] = 0;
}

static void click(int x, int y)
{
    in.mx = x;
    in.my = y;
    in.down = 1;
    in.pressed = 1;
    frame();
    in.down = 0;
    in.released = 1;
    frame();
    in.mx = in.my = -1;
    frame();
}

static void feed(double hz, int frames)
{
    size_t n = e.cfg.fft_size;
    double *buf = malloc(n * sizeof *buf);
    static double phase;
    for (int f = 0; f < frames; f++) {
        for (size_t i = 0; i < n; i++) {
            buf[i] = 0.5 * sin(phase);
            phase += 2 * M_PI * hz / e.cfg.sample_rate;
        }
        live_engine_process(&e, buf);
    }
    free(buf);
}

static int peak_x(void)
{
    double lo = e.cfg.f_min_hz, hi = e.cfg.f_max_hz;
    return g.spectrum.x + (int)((e.peak_hz - lo) / (hi - lo) * (g.spectrum.w - 1));
}

int main(int argc, char **argv)
{
    LiveEngineConfig c;
    memset(&c, 0, sizeof c);
    c.fft_size = 2048;
    c.window = SQDSP_WIN_HANN;
    c.sample_rate = 48000;
    c.f_max_hz = 24000;
    c.db_min = LIVE_DB_DEFAULT_MIN;
    c.db_max = LIVE_DB_DEFAULT_MAX;
    if (live_engine_init(&e, &c) != 0) {
        fprintf(stderr, "engine init failed\n");
        return 1;
    }
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;

    /* init: Human range, waterfall, fast scroll */
    live_gui_init(&g, &e, LIVE_DIR_WATERFALL, "demo");
    CHECK(g.range == LIVE_RANGE_HUMAN);
    CHECK(e.cfg.f_min_hz == 20 && e.cfg.f_max_hz == 16000);
    CHECK(live_gui_direction(&g) == LIVE_DIR_WATERFALL);
    CHECK(live_gui_scroll_skip(&g) == 1);

    feed(1000, 30);
    CHECK(fabs(e.peak_hz - 1000) < 30);
    frame();

    /* layout: toolbar, then spectrum over the LOFARgram, splitting the rest */
    int lh = sqg_line_h(&ui);
    CHECK(g.spectrum.y >= 2 * lh + 8);
    CHECK(g.spectrum.w == W && g.waterfall.w == W);
    CHECK(g.waterfall.y == g.spectrum.y + g.spectrum.h);
    CHECK(g.waterfall.y + g.waterfall.h == H);
    CHECK(abs(g.spectrum.h - g.waterfall.h) <= 1);

    /* spectrum: a trace across the width and the peak marker at 1 kHz */
    int trace = 0, cols = 0;
    for (int x = 0; x < W; x++) {
        int hit = 0;
        for (int y = g.spectrum.y; y < g.spectrum.y + g.spectrum.h; y++)
            if (get(x, y) == COL_TRACE) {
                trace++;
                hit = 1;
            }
        cols += hit;
    }
    CHECK(cols > W * 9 / 10);
    CHECK(trace > W);
    int pxk = peak_x(), marker = 0;
    for (int y = g.spectrum.y; y < g.spectrum.y + g.spectrum.h; y++)
        marker += get(pxk, y) == COL_PEAK;
    CHECK(marker > g.spectrum.h / 2);

    /* LOFARgram: newest rows at the top in waterfall mode */
    int top = g.waterfall.y + 2, bot = g.waterfall.y + g.waterfall.h - 1;
    uint32_t floor_col = get(W - 2, bot);
    CHECK(get(pxk, top) != floor_col);
    CHECK(get(pxk, bot) == floor_col);
    CHECK(get(pxk, top) != get(W - 2, top));

    /* toolbar row 1: Start/Stop, Hold, Reset, Fountain */
    int x = 4, y1 = 4 + lh / 2;
    int run_x = x + sqg_button_w(&ui, "Start") / 2;
    x += sqg_button_w(&ui, "Start") + 4;
    int hold_x = x + sqg_button_w(&ui, "Hold") / 2;
    x += sqg_button_w(&ui, "Hold") + 4;
    int reset_x = x + sqg_button_w(&ui, "Reset") / 2;
    x += sqg_button_w(&ui, "Reset") + 4;
    int fountain_x = x + sqg_button_w(&ui, "Fountain") / 2;

    click(run_x, y1);
    CHECK(g.paused == 1);
    click(run_x, y1);
    CHECK(g.paused == 0);

    CHECK(e.cfg.max_hold == 0);
    click(hold_x, y1);
    CHECK(e.cfg.max_hold == 1);
    feed(1000, 3);
    CHECK(e.mag_hold[live_engine_hz_to_bin(&e, 1000)] > -60);
    click(reset_x, y1);
    CHECK(e.mag_hold[live_engine_hz_to_bin(&e, 1000)] < -60);

    click(fountain_x, y1);
    CHECK(g.fountain == 1 && g.dir_changed == 1);
    CHECK(live_gui_direction(&g) == LIVE_DIR_FOUNTAIN);
    g.dir_changed = 0;
    frame();
    /* fountain: the newest rows moved to the bottom */
    CHECK(get(pxk, bot) != get(W - 2, bot));
    CHECK(get(pxk, top) == get(W - 2, top));

    /* toolbar row 2: range presets over three fifths, then speed */
    int y2 = 8 + lh + lh / 2, rw = (W - 8) * 3 / 5;
    click(4 + rw * 5 / 10, y2); /* Bass, the middle of five */
    CHECK(g.range == LIVE_RANGE_BASS);
    CHECK(e.cfg.f_min_hz == 20 && e.cfg.f_max_hz == 250);
    click(4 + rw * 7 / 10, y2); /* Speech */
    CHECK(g.range == LIVE_RANGE_SPEECH);
    CHECK(e.cfg.f_min_hz == 100 && e.cfg.f_max_hz == 8000);
    int sx = 4 + rw + 8, sw = W - 4 - sx;
    click(sx + sw / 6, y2); /* Slow */
    CHECK(g.speed == LIVE_SPEED_SLOW && live_gui_scroll_skip(&g) == 4);
    int taken = 0;
    for (int i = 0; i < 8; i++)
        taken += live_gui_take_frame(&g);
    CHECK(taken == 2);

    /* keyboard shortcuts */
    CHECK(live_gui_key(&g, &e, '5', 0) == 0);
    CHECK(g.range == LIVE_RANGE_FULL && e.cfg.f_max_hz == 24000);
    double span = e.cfg.f_max_hz - e.cfg.f_min_hz;
    live_gui_key(&g, &e, '[', 0);
    CHECK(g.range == -1 && e.cfg.f_max_hz - e.cfg.f_min_hz < span);
    double lo = e.cfg.f_min_hz;
    live_gui_key(&g, &e, 0, SQG_KEY_RIGHT);
    CHECK(e.cfg.f_min_hz > lo);
    live_gui_key(&g, &e, '1', 0);
    CHECK(g.range == LIVE_RANGE_HUMAN);
    live_gui_key(&g, &e, '9', 0);
    CHECK(g.speed == LIVE_SPEED_NORMAL);
    live_gui_key(&g, &e, ' ', 0);
    CHECK(g.paused);
    live_gui_key(&g, &e, ' ', 0);
    live_gui_key(&g, &e, 'w', 0);
    CHECK(!g.fountain && g.dir_changed);
    live_gui_key(&g, &e, 'm', 0);
    CHECK(e.cfg.max_hold == 0);
    CHECK(live_gui_key(&g, &e, 'q', 0) == 1);
    CHECK(live_gui_key(&g, &e, 0, SQG_KEY_ESCAPE) == 1);

    /* the wheel over the plots zooms around the pointer; over the toolbar it does not */
    span = e.cfg.f_max_hz - e.cfg.f_min_hz;
    in.mx = 4;
    in.my = 4;
    in.wheel = 1;
    frame();
    CHECK(e.cfg.f_max_hz - e.cfg.f_min_hz == span);
    in.mx = W / 4;
    in.my = g.spectrum.y + 10;
    in.wheel = 1;
    frame();
    CHECK(e.cfg.f_max_hz - e.cfg.f_min_hz < span);
    CHECK(g.range == -1);

    /* tiny canvas: nothing to draw into, but no crash */
    static uint32_t small[40 * 20];
    sqg_canvas sc;
    sqg_canvas_init(&sc, small, 40, 20, 40);
    sqg_begin(&ui, &sc, &in);
    live_gui_frame(&g, &ui, &e);
    sqg_end(&ui);

    /* a last frame for the snapshot */
    live_gui_key(&g, &e, '1', 0);
    feed(1000, 5);
    frame();
    if (argc > 1)
        CHECK(sqg_write_ppm(&cv, argv[1]) == 0);

    live_engine_free(&e);
    printf("test_live_gui: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

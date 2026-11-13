/* SquirrelCalc: view maths, axes and curves, and the window panel drawn
 * off screen. Usage: test_calc BUILD_DIR (writes BUILD_DIR/calc.ppm) */
#include "calcgui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
#define NEAR(a, b, tol) check(fabs((a) - (b)) <= (tol), #a " ~ " #b, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}

#define W 900
#define H 600
static uint32_t px[W * H];
static sqg_canvas cv;
static sqg_ui ui;
static sqg_input in;
static CalcGui g;

static int count(sqg_rect r, uint32_t col) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) n += px[y * W + x] == col;
    return n;
}

static void frame(void) {
    sqg_begin(&ui, &cv, &in);
    calc_gui_frame(&g, &ui);
    sqg_end(&ui);
    in.pressed = in.released = 0;
    in.wheel = 0;
    in.key = 0;
    in.text[0] = 0;
}

static void test_steps(void) {
    NEAR(calc_nice_step(20, 10), 2, 1e-12);
    NEAR(calc_nice_step(1, 5), 0.2, 1e-12);
    NEAR(calc_nice_step(7, 7), 1, 1e-12);
    NEAR(calc_nice_step(0.03, 3), 0.01, 1e-15);
    NEAR(calc_nice_step(3000, 4), 1000, 1e-9);
    CHECK(calc_nice_step(0, 4) == 1 && calc_nice_step(-1, 4) == 1);
    char t[32];
    calc_tick_label(0.2, 0.2, t, sizeof t);
    CHECK(!strcmp(t, "0.2"));
    calc_tick_label(3, 1, t, sizeof t);
    CHECK(!strcmp(t, "3"));
    calc_tick_label(0.30000000000000004, 0.1, t, sizeof t);
    CHECK(!strcmp(t, "0.3"));
    calc_tick_label(0.05, 0.05, t, sizeof t);
    CHECK(!strcmp(t, "0.05"));
    calc_tick_label(-4e-17, 0.1, t, sizeof t);
    CHECK(!strcmp(t, "0.0"));
    calc_tick_label(2e7, 1e7, t, sizeof t);
    CHECK(!strcmp(t, "2e+07"));
}

static void test_view(void) {
    CalcView v;
    calc_view_default(&v, 400, 200);
    NEAR(v.xmin, -10, 1e-12);
    NEAR(v.xmax, 10, 1e-12);
    NEAR(v.ymin, -5, 1e-12);
    NEAR(v.ymax, 5, 1e-12);
    sqg_rect r = sqg_rect_make(50, 20, 400, 200);
    NEAR(calc_px_x(&v, r, 0), 250, 1e-9);
    NEAR(calc_px_y(&v, r, 0), 120, 1e-9);
    NEAR(calc_px_y(&v, r, 5), 20, 1e-9);
    NEAR(calc_x_at(&v, r, calc_px_x(&v, r, 3.7)), 3.7, 1e-9);
    NEAR(calc_y_at(&v, r, calc_px_y(&v, r, -2.2)), -2.2, 1e-9);
    /* zoom keeps the point under the pointer */
    double before_x = calc_px_x(&v, r, 3), before_y = calc_px_y(&v, r, 1);
    calc_view_zoom(&v, 2, 3, 1);
    NEAR(v.xmax - v.xmin, 10, 1e-9);
    NEAR(v.ymax - v.ymin, 5, 1e-9);
    NEAR(calc_px_x(&v, r, 3), before_x, 1e-9);
    NEAR(calc_px_y(&v, r, 1), before_y, 1e-9);
    CalcView same = v;
    calc_view_zoom(&v, 0, 0, 0);
    calc_view_zoom(&v, NAN, 0, 0);
    CHECK(!memcmp(&v, &same, sizeof v));
    for (int i = 0; i < 200; i++) calc_view_zoom(&v, 10, 0, 0);
    CHECK(v.xmax - v.xmin >= 1e-9 && v.xmax > v.xmin);
    /* panning drags the plane with the pointer */
    calc_view_default(&v, 400, 200);
    calc_view_pan(&v, 40, -20, 400, 200);
    NEAR(v.xmin, -12, 1e-9);
    NEAR(v.ymin, -6, 1e-9);
    NEAR(v.ymax - v.ymin, 10, 1e-9);
}

static int compile1(const char *text, SqmProgram *p) {
    static SqmExpr e;
    SqmError err;
    const char *names[] = {"x"};
    if (sqm_parse(text, &e, &err) != SQM_OK) return -1;
    return sqm_compile(&e, names, 1, p) == SQM_OK ? 0 : -1;
}

static void test_plot(void) {
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_rect r = sqg_rect_make(100, 100, 400, 400);
    CalcView v;
    calc_view_default(&v, r.w, r.h);
    calc_draw_axes(&cv, r, &v);
    CHECK(count(r, 0xFF404850u) >= 2 * 390); /* both axes */
    CHECK(px[0] == 0);                       /* nothing outside the rectangle */

    SqmProgram p;
    double values[1];
    const uint32_t RED = 0xFFFF0000u;
    CHECK(compile1("x", &p) == 0);
    int segs = calc_plot(&cv, r, &v, &p, values, 0, RED);
    CHECK(segs >= 2 * r.w - 2);
    /* y = x goes through the corners and the centre */
    CHECK(count(sqg_rect_make(297, 297, 6, 6), RED) > 0);
    CHECK(count(sqg_rect_make(100, 494, 6, 6), RED) > 0);
    CHECK(count(sqg_rect_make(494, 100, 6, 6), RED) > 0);
    CHECK(count(sqg_rect_make(100, 100, 50, 50), RED) == 0);

    /* 1/x: no line joining -inf to +inf at the pole */
    calc_draw_axes(&cv, r, &v);
    CHECK(compile1("1/x", &p) == 0);
    CHECK(calc_plot(&cv, r, &v, &p, values, 0, RED) > 100);
    CHECK(count(sqg_rect_make(300, 140, 2, 120), RED) == 0);
    CHECK(count(sqg_rect_make(300, 340, 2, 120), RED) == 0);
    CHECK(count(sqg_rect_make(440, 294, 60, 6), RED) > 0); /* x 7..10, y = 1/x just above the axis */

    /* sqrt(x): nothing left of 0; a constant off screen: nothing at all */
    calc_draw_axes(&cv, r, &v);
    CHECK(compile1("sqrt(x)", &p) == 0);
    calc_plot(&cv, r, &v, &p, values, 0, RED);
    CHECK(count(sqg_rect_make(100, 100, 195, 400), RED) == 0);
    CHECK(count(sqg_rect_make(305, 100, 195, 400), RED) > 0);
    calc_draw_axes(&cv, r, &v);
    CHECK(compile1("50", &p) == 0);
    CHECK(calc_plot(&cv, r, &v, &p, values, 0, RED) == 0);
    CHECK(count(r, RED) == 0);

    /* far away views must not hang or crash */
    CalcView far = {1e11, 1e11 + 1e-9, -1, 1};
    CHECK(compile1("sin(x)", &p) == 0);
    calc_draw_axes(&cv, r, &far);
    calc_plot(&cv, r, &far, &p, values, 0, RED);

    /* shading y = 1 from 0 to 5: 100 columns at 20 px per unit, none outside */
    calc_draw_axes(&cv, r, &v);
    CHECK(compile1("1", &p) == 0);
    CHECK(calc_shade(&cv, r, &v, &p, values, 0, 5, 0, 0x80FF0000u) == 100);
    uint32_t shaded = px[290 * W + 350];
    CHECK(shaded != 0xFFFFFFFFu && ((shaded >> 16) & 0xFF) > ((shaded >> 8) & 0xFF));
    CHECK(px[290 * W + 290] == 0xFFFFFFFFu || px[290 * W + 290] == 0xFFE4E8EEu);
    CHECK(px[270 * W + 350] == 0xFFFFFFFFu || px[270 * W + 350] == 0xFFE4E8EEu); /* above y = 1 */
    CHECK(calc_shade(&cv, r, &v, &p, values, 0, NAN, 1, 0x80FF0000u) == 0);
    double pxs[2] = {2, 50}, pys[2] = {3, 0};
    CHECK(calc_mark_points(&cv, r, &v, pxs, pys, 2, RED) == 1);
    CHECK(count(sqg_rect_make(337, 237, 6, 6), RED) > 0);

    /* a circle of radius 5 as a curve: through (5, 0), (0, 5), (-5, 0), not the middle */
    calc_draw_axes(&cv, r, &v);
    SqmProgram cx, cy;
    static SqmExpr ce;
    const char *T[] = {"t"};
    CHECK(sqm_parse("5 cos(t)", &ce, NULL) == SQM_OK && sqm_compile(&ce, T, 1, &cx) == SQM_OK);
    CHECK(sqm_parse("5 sin(t)", &ce, NULL) == SQM_OK && sqm_compile(&ce, T, 1, &cy) == SQM_OK);
    CHECK(calc_plot_curve(&cv, r, &v, &cx, &cy, values, 0, 0, 6.283185307179586, 400, RED) == 400);
    CHECK(count(sqg_rect_make(397, 296, 6, 8), RED) > 0);
    CHECK(count(sqg_rect_make(296, 197, 8, 6), RED) > 0);
    CHECK(count(sqg_rect_make(197, 296, 6, 8), RED) > 0);
    CHECK(count(sqg_rect_make(280, 280, 40, 40), RED) == 0);
    CHECK(calc_plot_curve(&cv, r, &v, &cx, &cy, values, 0, 0, NAN, 400, RED) == 0);
    /* half of it: nothing below the axis */
    calc_draw_axes(&cv, r, &v);
    calc_plot_curve(&cv, r, &v, &cx, &cy, values, 0, 0, 3.14159, 400, RED);
    CHECK(count(sqg_rect_make(100, 303, 400, 197), RED) == 0);

    double xs[3] = {0, 5, 50};
    CHECK(calc_mark_roots(&cv, r, &v, xs, 3, RED) == 2);
    CHECK(count(sqg_rect_make(397, 297, 6, 6), RED) > 0);
}

static int contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void test_gui(const char *dir) {
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    calc_gui_init(&g);

    /* functions and their errors */
    calc_gui_set(&g, 0, "x^2 - 2");
    CHECK(g.ok[0] && !g.err[0][0]);
    calc_gui_set(&g, 1, "x^");
    CHECK(!g.ok[1] && g.err[1][0]);
    calc_gui_set(&g, 1, "a sin(x)");
    CHECK(!g.ok[1] && contains(g.err[1], "a = 1"));

    /* the calculator line */
    CHECK(calc_gui_eval(&g, "2+3") == 0);
    NEAR(calc_gui_var(&g, "ans"), 5, 0);
    CHECK(calc_gui_eval(&g, "ans*2") == 0);
    NEAR(calc_gui_var(&g, "ans"), 10, 0);
    CHECK(contains(g.hist, "2+3 = 5") && contains(g.hist, "ans*2 = 10"));
    CHECK(calc_gui_eval(&g, "a = 2") == 0);
    NEAR(calc_gui_var(&g, "a"), 2, 0);
    CHECK(g.ok[1]); /* y2 = a sin(x) now draws */
    CHECK(calc_gui_eval(&g, "a^3 + 1") == 0);
    NEAR(calc_gui_var(&g, "ans"), 9, 0);
    CHECK(calc_gui_eval(&g, "x = 3") == -1);
    CHECK(calc_gui_eval(&g, "pi = 3") == -1);
    CHECK(calc_gui_eval(&g, "2 = 3") == -1);
    CHECK(calc_gui_eval(&g, "q + 1") == -1 && contains(g.hist, "q is not defined"));
    CHECK(calc_gui_eval(&g, "3 +") == -1);
    CHECK(isnan(calc_gui_var(&g, "nope")));
    CHECK(calc_gui_eval(&g, "a = a + 1") == 0);
    NEAR(calc_gui_var(&g, "a"), 3, 0);

    /* CAS lines */
    g.hist[0] = 0;
    CHECK(calc_gui_eval(&g, "expand (x+1)^2") == 0 && contains(g.hist, "expand (x+1)^2 = x^2 + 2*x + 1"));
    CHECK(calc_gui_eval(&g, "d/dx x^3 + a x") == 0 && contains(g.hist, "= 3*x^2 + a"));
    CHECK(calc_gui_eval(&g, "collect x a x + b x") == 0 && contains(g.hist, "= (a + b)*x"));
    CHECK(calc_gui_eval(&g, "solve x^2 - x = 1") == 0);
    CHECK(contains(g.hist, "\n  x = (1 - sqrt(5))/2  (-0.618034)\n  x = (1 + sqrt(5))/2  (1.61803)"));
    NEAR(calc_gui_var(&g, "ans"), (1 - sqrt(5)) / 2, 1e-12);
    CHECK(calc_gui_eval(&g, "solve 2x + 4 = 0") == 0 && contains(g.hist, "  x = -2"));
    CHECK(calc_gui_eval(&g, "solve k t + 1 = 0 for t") == 0 && contains(g.hist, "  t = -1/k"));
    CHECK(calc_gui_eval(&g, "solve a t = 1 for t") == 0 && contains(g.hist, "  t = 1/a  (0.333333)")); /* a = 3 */
    CHECK(calc_gui_eval(&g, "solve x^2 = -4") == 0 && contains(g.hist, "no real solution"));
    CHECK(calc_gui_eval(&g, "solve x^3 = 2") == -1 && contains(g.hist, "can't solve this exactly"));
    CHECK(calc_gui_eval(&g, "solve x = 1 = 2") == -1 && contains(g.hist, "more than one ="));
    CHECK(calc_gui_eval(&g, "solve x = 1 for 2") == -1);
    CHECK(calc_gui_eval(&g, "collect x") == -1);
    CHECK(calc_gui_eval(&g, "expand (") == -1);
    CHECK(calc_gui_eval(&g, "expanded = 4") == 0); /* a name, not the command */
    NEAR(calc_gui_var(&g, "expanded"), 4, 0);

    /* a drawn frame: both curves, roots of y1 at +-sqrt(2) */
    g.show_roots = 1;
    frame();
    frame();
    CHECK(g.nroots[0] == 2);
    if (g.nroots[0] == 2) {
        NEAR(g.roots[0][0], -sqrt(2), 1e-9);
        NEAR(g.roots[0][1], sqrt(2), 1e-9);
    }
    CHECK(contains(g.status, "1.41421"));
    sqg_rect gr = g.graph_at;
    CHECK(gr.w > 400 && gr.h > 400 && gr.x > 240);
    CHECK(count(gr, 0xFF2060D0u) > 200);
    CHECK(count(gr, 0xFFD03030u) > 200);

    /* typing into y3 is noticed by the next frame */
    snprintf(g.fx[2], sizeof g.fx[2], "x/2");
    frame();
    CHECK(g.ok[2] && count(gr, 0xFF20A040u) > 200);

    /* the derivative of y1 */
    g.show_deriv = 1;
    frame();
    CHECK(g.dok && !strcmp(g.dtext, "y1' = 2*x"));
    if (sqg_write_ppm(&cv, "calc.ppm") == 0) remove("calc.ppm");
    char path[512];
    snprintf(path, sizeof path, "%s/calc.ppm", dir);
    CHECK(sqg_write_ppm(&cv, path) == 0);

    /* the wheel zooms about the pointer */
    sqg_rect inner = sqg_rect_inset(gr, 2);
    in.mx = inner.x + inner.w / 4;
    in.my = inner.y + inner.h / 3;
    frame();
    double span = g.view.xmax - g.view.xmin, mx = g.mouse_x;
    CHECK(g.mouse_in);
    in.wheel = 1;
    frame();
    NEAR(g.view.xmax - g.view.xmin, span / 1.25, 1e-9);
    frame();
    NEAR(g.mouse_x, mx, span / inner.w * 1.01);

    /* a drag pans */
    double xmin = g.view.xmin, per = (g.view.xmax - g.view.xmin) / inner.w;
    in.down = in.pressed = 1;
    frame();
    in.mx += 50;
    frame();
    in.down = 0;
    in.released = 1;
    frame();
    NEAR(g.view.xmin, xmin - 50 * per, per * 0.01);
    CHECK(!g.dragging);

    /* Reset brings back -10..10 */
    in.mx = in.my = -1;
    frame();
    g.view_ready = 0;
    frame();
    NEAR(g.view.xmin, -10, 1e-9);
    NEAR(g.view.xmax, 10, 1e-9);

    /* the zero-size and tiny window cases do not crash */
    static uint32_t small[60 * 40];
    sqg_canvas sc;
    sqg_canvas_init(&sc, small, 60, 40, 60);
    sqg_begin(&ui, &sc, &in);
    calc_gui_frame(&g, &ui);
    sqg_end(&ui);
}

static void click(int x, int y) {
    in.mx = x;
    in.my = y;
    in.down = in.pressed = 1;
    frame();
    in.down = 0;
    in.released = 1;
    frame();
}

static int differ(const uint32_t *a, const uint32_t *b, int n) {
    int d = 0;
    for (int i = 0; i < n; i++) d += a[i] != b[i];
    return d;
}

static void test_analysis(void) {
    static uint32_t before[W * H];
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    calc_gui_init(&g);
    calc_gui_set(&g, 0, "x^2 - 2");
    calc_gui_set(&g, 1, "x");

    /* crossings of y1 and y2 at x = -1 and 2 */
    g.show_cross = 1;
    frame();
    CHECK(g.ncross == 2);
    if (g.ncross == 2) {
        NEAR(g.cross_x[0], -1, 1e-9);
        NEAR(g.cross_y[0], -1, 1e-9);
        NEAR(g.cross_x[1], 2, 1e-9);
        NEAR(g.cross_y[1], 2, 1e-9);
    }
    CHECK(contains(g.status, "Crossings  y1=y2 at (-1, -1)  y1=y2 at (2, 2)"));
    calc_gui_set(&g, 1, "");
    frame();
    CHECK(g.ncross == 0 && contains(g.status, "Crossings none in view"));
    calc_gui_set(&g, 1, "x");
    g.show_cross = 0;
    frame();
    CHECK(contains(g.status, "click a curve"));

    /* integrals, and the shaded area */
    memcpy(before, px, sizeof before);
    CHECK(calc_gui_eval(&g, "integrate y1 from 0 to 3") == 0 && contains(g.hist, "integrate y1 from 0 to 3 = 3"));
    NEAR(calc_gui_var(&g, "ans"), 3, 1e-9);
    CHECK(g.area_on && g.area_fn == 0);
    frame();
    CHECK(differ(before, px, W * H) > 1000);
    CHECK(calc_gui_eval(&g, "integrate x^2 from 0 to 1") == 0 && contains(g.hist, "= 0.333333333333"));
    CHECK(g.area_on && g.area_fn == -1);
    CHECK(calc_gui_eval(&g, "k = 2") == 0);
    CHECK(calc_gui_eval(&g, "integrate k x from 0 to k") == 0 && contains(g.hist, "integrate k x from 0 to k = 4"));
    CHECK(calc_gui_eval(&g, "integrate sin(x) from 0 to pi") == 0 && contains(g.hist, "= 2"));
    CHECK(calc_gui_eval(&g, "integrate y4 from 0 to 1") == -1 && contains(g.hist, "that function is empty"));
    CHECK(calc_gui_eval(&g, "integrate x from 0") == -1 && contains(g.hist, "write integrate EXPR from A to B"));
    CHECK(calc_gui_eval(&g, "integrate x from 0 to q") == -1 && contains(g.hist, "the bounds need values"));
    CHECK(calc_gui_eval(&g, "integrate q x from 0 to 1") == -1 && contains(g.hist, "a name has no value"));
    CHECK(calc_gui_eval(&g, "integrate y1 from 0 to 1") == 0 && g.area_fn == 0);
    /* units */
    CHECK(calc_gui_eval(&g, "5 ft to m") == 0 && contains(g.hist, "5 ft to m = 1.524 m"));
    CHECK(calc_gui_eval(&g, "ans * 2") == 0 && contains(g.hist, "= 3.048"));
    CHECK(calc_gui_eval(&g, "65 mph -> km/h") == 0 && contains(g.hist, "= 104.60736 km/h"));
    CHECK(calc_gui_eval(&g, "units 5 kg * 9.81 m/s^2") == 0 && contains(g.hist, "= 49.05 N"));
    CHECK(calc_gui_eval(&g, "convert 20 degC to degF") == 0 && contains(g.hist, "= 68 degF"));
    CHECK(calc_gui_eval(&g, "35.681236 deg to dms") == 0 && contains(g.hist, "= 35\xc2\xb0" "40'52.45\""));
    CHECK(calc_gui_eval(&g, "5 m to kg") == -1 && contains(g.hist, "units don't match"));
    CHECK(calc_gui_eval(&g, "3 m + 2 s to m") == -1 && contains(g.hist, "adding different kinds of units"));
    CHECK(calc_gui_eval(&g, "units 2 furlong") == -1 && contains(g.hist, "unknown unit at \"furlong\""));
    calc_gui_set(&g, 0, "x^2 - 3"); /* the area was of the old y1 */
    CHECK(!g.area_on);
    calc_gui_set(&g, 0, "x^2 - 2");

    /* trace: a click near y1 at x = 1 pins it there; Right steps one pixel */
    frame();
    sqg_rect inner = sqg_rect_inset(g.graph_at, 2);
    int cx = (int)calc_px_x(&g.view, inner, 1), cy = (int)calc_px_y(&g.view, inner, -1);
    CalcView keep = g.view;
    click(cx, cy + 3);
    CHECK(g.trace_fn == 0);
    NEAR(g.trace_x, 1, (g.view.xmax - g.view.xmin) / inner.w);
    CHECK(!memcmp(&keep, &g.view, sizeof keep)); /* a click does not pan */
    double tx = g.trace_x;
    in.key = SQG_KEY_RIGHT;
    frame();
    NEAR(g.trace_x, tx + (g.view.xmax - g.view.xmin) / inner.w, 1e-12);
    CHECK(count(sqg_rect_make(cx - 6, cy - 6, 12, 12), 0xFF2060D0u) > 10);
    click(cx, (int)calc_px_y(&g.view, inner, 1) + 1); /* on y2 = x at x = 1 */
    CHECK(g.trace_fn == 1);
    click(inner.x + 20, inner.y + 20); /* nothing there */
    CHECK(g.trace_fn == -1);
    click(cx, cy);
    CHECK(g.trace_fn == 0);
    calc_gui_set(&g, 0, "x^"); /* an error stops the trace */
    CHECK(g.trace_fn == -1);
    calc_gui_set(&g, 0, "x^2 - 2");

    /* the table */
    g.show_table = 1;
    in.mx = in.my = -1;
    frame();
    CHECK(!g.mouse_in);
    in.mx = inner.x + 100;
    in.my = inner.y + 200;
    in.wheel = -1;
    frame();
    CHECK(g.table_top == 3);
    snprintf(g.tstep, sizeof g.tstep, "0");
    frame(); /* an error message, not a hang */
    snprintf(g.tstep, sizeof g.tstep, "pi/4");
    frame();
    g.show_table = 0;
    frame();
}

static void test_curves(void) {
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    calc_gui_init(&g);

    calc_gui_set(&g, 0, "(cos(t), sin(t))");
    CHECK(g.ok[0] && g.kind[0] == CALC_PARAM);
    calc_gui_set(&g, 1, "r = 2");
    CHECK(g.ok[1] && g.kind[1] == CALC_POLAR);
    calc_gui_set(&g, 2, "  r=1+cos(t)");
    CHECK(g.ok[2] && g.kind[2] == CALC_POLAR);
    calc_gui_set(&g, 3, "(x + 1)");
    CHECK(g.ok[3] && g.kind[3] == CALC_FUNC); /* one part: a function */
    calc_gui_set(&g, 3, "(x, t)");
    CHECK(!g.ok[3] && !strcmp(g.err[3], "curves use t, not x"));
    calc_gui_set(&g, 3, "r = q t");
    CHECK(!g.ok[3] && contains(g.err[3], "define q first"));
    calc_gui_set(&g, 3, "(cos(t), sin(t) +)");
    CHECK(!g.ok[3] && contains(g.err[3], "(at 18)"));
    calc_gui_set(&g, 3, "(1, 2, 3)");
    CHECK(!g.ok[3] && g.kind[3] == CALC_FUNC);
    calc_gui_set(&g, 3, "");

    /* drawn: the unit circle and the circle of radius 2 */
    frame();
    sqg_rect inner = sqg_rect_inset(g.graph_at, 2);
    int ox = (int)calc_px_x(&g.view, inner, 0), oy = (int)calc_px_y(&g.view, inner, 0);
    int u = (int)(calc_px_x(&g.view, inner, 1) - ox);
    CHECK(count(sqg_rect_make(ox + u - 3, oy - 6, 6, 12), 0xFF2060D0u) > 0);
    CHECK(count(sqg_rect_make(ox - 6, oy - u - 3, 12, 6), 0xFF2060D0u) > 0);
    CHECK(count(sqg_rect_make(ox - 2 * u - 3, oy - 6, 6, 12), 0xFFD03030u) > 0);
    CHECK(count(sqg_rect_make(ox - u / 2, oy - u / 2, u, u), 0xFFD03030u) == 0);
    /* curves have no roots, crossings, derivative, integral or table column */
    g.show_roots = g.show_cross = g.show_deriv = 1;
    frame();
    CHECK(g.nroots[0] == 0 && g.ncross == 0 && !g.dok);
    CHECK(calc_gui_eval(&g, "integrate y1 from 0 to 1") == -1 && contains(g.hist, "is a curve"));
    /* tmax = pi: the upper half only */
    CHECK(calc_gui_eval(&g, "tmax = pi") == 0);
    g.show_roots = g.show_cross = g.show_deriv = 0;
    frame();
    CHECK(count(sqg_rect_make(ox - 6, oy + u - 3, 12, 6), 0xFF2060D0u) == 0);
    CHECK(count(sqg_rect_make(ox - 6, oy - u - 3, 12, 6), 0xFF2060D0u) > 0);

    /* sliders: one per variable but ans; dragging one moves the graph */
    calc_gui_init(&g);
    CHECK(calc_gui_eval(&g, "1 + 1") == 0); /* ans: no slider */
    CHECK(calc_gui_eval(&g, "a = 2") == 0);
    NEAR(g.vlo[0] + g.vlo[1], -10, 0); /* ans has none; a has -10..10 */
    calc_gui_set(&g, 0, "a");
    frame();
    sqg_rect sr = g.slider_at[0];
    CHECK(sr.w > 40 && g.slider_at[1].w == 0);
    in.mx = sr.x + sr.w * 3 / 4;
    in.my = sr.y + sr.h / 2;
    in.down = in.pressed = 1;
    frame();
    in.down = 0;
    in.released = 1;
    frame();
    double a = calc_gui_var(&g, "a");
    CHECK(a > 3 && a < 7);
    inner = sqg_rect_inset(g.graph_at, 2);
    int ya = (int)calc_px_y(&g.view, inner, a);
    CHECK(count(sqg_rect_make(inner.x, ya - 2, inner.w, 5), 0xFF2060D0u) > inner.w / 2);
    CHECK(calc_gui_eval(&g, "a = 100") == 0);
    CHECK(calc_gui_eval(&g, "b = -3") == 0 && calc_gui_eval(&g, "c = 1") == 0 && calc_gui_eval(&g, "d = 1") == 0 &&
          calc_gui_eval(&g, "f = 1") == 0);
    frame();
    for (int k = 0; k < CALC_SLIDERS; k++) CHECK(g.slider_at[k].w > 0);
    for (int i = 0; i < g.nvars; i++)
        if (!strcmp(g.vname[i], "a")) CHECK(g.vlo[i] == -200 && g.vhi[i] == 200);
}

static int compile_xy2(const char *text, SqmProgram *p) {
    static SqmExpr e;
    SqmError err;
    const char *names[] = {"x", "y"};
    if (sqm_parse(text, &e, &err) != SQM_OK) return -1;
    return sqm_compile(&e, names, 2, p) == SQM_OK ? 0 : -1;
}

static void clear(void) { memset(px, 0xFF, sizeof px); }

/* the pixel of the plane point (x, y) on r for v */
static int pxx(const CalcView *v, sqg_rect r, double x) { return (int)calc_px_x(v, r, x); }
static int pxy(const CalcView *v, sqg_rect r, double y) { return (int)calc_px_y(v, r, y); }

static void test_implicit(void) {
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_rect r = sqg_rect_make(100, 100, 400, 400);
    CalcView v;
    calc_view_default(&v, r.w, r.h);
    const uint32_t RED = 0xFFFF0000u;
    SqmProgram p;
    double xy[2];

    /* the circle of radius 5: on it at (3, 4), (-5, 0), (0, -5); not at the
     * centre or out at (8, 8) */
    clear();
    CHECK(compile_xy2("x^2 + y^2 - 25", &p) == 0);
    int pieces = calc_plot_implicit(&cv, r, &v, &p, xy, 0, 1, 3, 0, RED);
    CHECK(pieces > 100);
    CHECK(count(sqg_rect_make(pxx(&v, r, 3) - 2, pxy(&v, r, 4) - 2, 5, 5), RED) > 0);
    CHECK(count(sqg_rect_make(pxx(&v, r, -5) - 2, pxy(&v, r, 0) - 2, 5, 5), RED) > 0);
    CHECK(count(sqg_rect_make(pxx(&v, r, 0) - 2, pxy(&v, r, -5) - 2, 5, 5), RED) > 0);
    CHECK(count(sqg_rect_make(pxx(&v, r, -3), pxy(&v, r, 3), 6 * 20, 6 * 20), RED) == 0); /* inside stays clear */
    CHECK(count(sqg_rect_make(pxx(&v, r, 7), pxy(&v, r, 9), 40, 40), RED) == 0);
    CHECK(px[0] != RED && count(sqg_rect_make(0, 0, 100, H), RED) == 0); /* nothing outside r */

    /* dashed: fewer pieces drawn, but still some */
    clear();
    int dashed = calc_plot_implicit(&cv, r, &v, &p, xy, 0, 1, 3, 1, RED);
    CHECK(dashed == pieces);
    int solid_px = 0, dash_px = count(r, RED);
    clear();
    calc_plot_implicit(&cv, r, &v, &p, xy, 0, 1, 3, 0, RED);
    solid_px = count(r, RED);
    CHECK(dash_px > solid_px / 4 && dash_px < solid_px * 3 / 4);

    /* x y = 0 is both axes: every cell along them is a saddle or an edge */
    clear();
    CHECK(compile_xy2("x y", &p) == 0);
    calc_plot_implicit(&cv, r, &v, &p, xy, 0, 1, 4, 0, RED);
    CHECK(count(sqg_rect_make(pxx(&v, r, 0) - 2, r.y + 10, 5, r.h - 20), RED) > r.h - 40);
    CHECK(count(sqg_rect_make(r.x + 10, pxy(&v, r, 0) - 2, r.w - 20, 5), RED) > r.w - 40);

    /* y = 1/x: no line down the pole at x = 0 */
    clear();
    CHECK(compile_xy2("y - 1/x", &p) == 0);
    CHECK(calc_plot_implicit(&cv, r, &v, &p, xy, 0, 1, 3, 0, RED) > 50);
    /* (beyond |y| = 5 the branches themselves come within 3 pixels of x = 0) */
    CHECK(count(sqg_rect_make(pxx(&v, r, 0) - 2, pxy(&v, r, 5), 5, pxy(&v, r, -5) - pxy(&v, r, 5)), RED) == 0);
    CHECK(count(sqg_rect_make(pxx(&v, r, 1) - 3, pxy(&v, r, 1) - 3, 7, 7), RED) > 0);

    /* x = 3: a vertical line */
    clear();
    CHECK(compile_xy2("x - 3", &p) == 0);
    calc_plot_implicit(&cv, r, &v, &p, xy, 0, 1, 3, 0, RED);
    CHECK(count(sqg_rect_make(pxx(&v, r, 3) - 2, r.y, 5, r.h), RED) > r.h - 10);
    CHECK(count(r, RED) < 3 * r.h);

    /* nothing: x^2 + y^2 + 1 = 0, and an empty program */
    clear();
    CHECK(compile_xy2("x^2 + y^2 + 1", &p) == 0);
    CHECK(calc_plot_implicit(&cv, r, &v, &p, xy, 0, 1, 3, 0, RED) == 0);
    SqmProgram none;
    memset(&none, 0, sizeof none);
    CHECK(calc_plot_implicit(&cv, r, &v, &none, xy, 0, 1, 3, 0, RED) == 0);
    CHECK(calc_shade_region(&cv, r, &v, &none, xy, 0, 1, 3, RED) == 0);
    /* a huge cell count is capped by a coarser grid, not an overflow */
    sqg_rect wide = sqg_rect_make(0, 0, W, 10);
    CHECK(compile_xy2("x", &p) == 0);
    CHECK(calc_plot_implicit(&cv, wide, &v, &p, xy, 0, 1, 0, 0, RED) >= 1);

    /* the disc x^2 + y^2 < 9: blocks shaded add up to its area */
    clear();
    CHECK(compile_xy2("x^2 + y^2 - 9", &p) == 0);
    int blocks = calc_shade_region(&cv, r, &v, &p, xy, 0, 1, 2, 0x80000000u);
    double per_block = (20.0 / 400 * 2) * (20.0 / 400 * 2);
    NEAR(blocks * per_block, 9 * 3.14159265358979, 0.5);
    CHECK(px[pxy(&v, r, 0) * W + pxx(&v, r, 0)] != 0xFFFFFFFFu);
    CHECK(px[pxy(&v, r, 8) * W + pxx(&v, r, 8)] == 0xFFFFFFFFu);

    /* a fitted curve through a C function: y = x */
    clear();
    CalcFit f = {CALC_FIT_LINEAR, {0, 1, 0}, 1, 1, 2};
    extern double test_fit_y(const void *ctx, double x);
    CHECK(calc_plot_fn(&cv, r, &v, test_fit_y, &f, RED) >= 2 * r.w - 2);
    CHECK(count(sqg_rect_make(297, 297, 6, 6), RED) > 0);
}

double test_fit_y(const void *ctx, double x) { return calc_fit_eval((const CalcFit *)ctx, x); }

static void test_stats(void) {
    CalcStats st;
    const double odd[] = {5, 1, 4, 2, 3}, even[] = {10, 2, 4, 8};
    CHECK(calc_stats(odd, 5, &st) == 0 && st.n == 5);
    NEAR(st.mean, 3, 1e-15);
    NEAR(st.median, 3, 0);
    NEAR(st.sd, sqrt(2.5), 1e-15);
    CHECK(st.min == 1 && st.max == 5 && st.sum == 15);
    CHECK(calc_stats(even, 4, &st) == 0);
    NEAR(st.median, 6, 0);
    CHECK(calc_stats(even, 1, &st) == 0 && st.sd == 0 && st.median == 10);
    const double bad[] = {1, NAN};
    CHECK(calc_stats(bad, 2, &st) == -1 && calc_stats(odd, 0, &st) == -1 && calc_stats(odd, 101, &st) == -1);

    /* the worked example (checked against an independent fit) */
    const double xs[] = {1, 2, 3, 4, 5, 6}, ys[] = {2.1, 3.9, 6.2, 7.8, 10.1, 12.2};
    CalcFit f;
    const char *why = NULL;
    char t[160];
    CHECK(calc_fit(CALC_FIT_LINEAR, xs, ys, 6, &f, &why) == 0);
    NEAR(f.c[1], 2.02, 1e-12);
    NEAR(f.c[0], -0.02, 1e-12);
    NEAR(f.r2, 0.9982106661074994, 1e-12);
    NEAR(f.r * f.r, f.r2, 1e-12);
    calc_fit_text(&f, t, sizeof t);
    CHECK(!strcmp(t, "y = 2.02 x - 0.02"));
    CHECK(calc_fit(CALC_FIT_QUAD, xs, ys, 6, &f, &why) == 0);
    NEAR(f.c[2], 0.0267857142857, 1e-10);
    NEAR(f.c[1], 1.8325, 1e-10);
    NEAR(f.c[0], 0.23, 1e-10);
    calc_fit_text(&f, t, sizeof t);
    CHECK(!strcmp(t, "y = 0.0267857 x^2 + 1.8325 x + 0.23"));
    const double y2[] = {1, 2.7, 7.4, 20, 54.6, 148};
    CHECK(calc_fit(CALC_FIT_EXP, xs, y2, 6, &f, &why) == 0);
    NEAR(f.c[0], 0.3671024566006578, 1e-12);
    NEAR(f.c[1], 1.0000188563253194, 1e-12);
    NEAR(f.r2, 0.9999982178959937, 1e-12);
    calc_fit_text(&f, t, sizeof t);
    CHECK(!strcmp(t, "y = 0.367102 e^(1.00002 x)"));

    /* exact data, far from 0 (the quadratic stays well conditioned) */
    double qx[8], qy[8];
    for (int i = 0; i < 8; i++) {
        qx[i] = 1000 + 0.5 * i;
        qy[i] = 3 * qx[i] * qx[i] - 2 * qx[i] + 1;
    }
    CHECK(calc_fit(CALC_FIT_QUAD, qx, qy, 8, &f, NULL) == 0);
    NEAR(f.c[2], 3, 1e-6);
    NEAR(f.c[1], -2, 1e-2);
    NEAR(calc_fit_eval(&f, 1001.25), 3 * 1001.25 * 1001.25 - 2 * 1001.25 + 1, 1e-4);
    NEAR(f.r2, 1, 1e-12);
    for (int i = 0; i < 8; i++) qy[i] = 4 * exp(-0.5 * qx[i] / 1000);
    CHECK(calc_fit(CALC_FIT_EXP, qx, qy, 8, &f, NULL) == 0);
    NEAR(f.c[1], -0.0005, 1e-12);
    NEAR(f.c[0], 4, 1e-9);
    const double flat[] = {2, 2, 2};
    CHECK(calc_fit(CALC_FIT_LINEAR, xs, flat, 3, &f, NULL) == 0 && f.c[1] == 0 && f.r2 == 1);
    calc_fit_text(&f, t, sizeof t);
    CHECK(!strcmp(t, "y = 0 x + 2"));

    /* refusals */
    CHECK(calc_fit(CALC_FIT_LINEAR, xs, ys, 1, &f, &why) == -1 && contains(why, "2 points"));
    CHECK(calc_fit(CALC_FIT_QUAD, xs, ys, 2, &f, &why) == -1 && contains(why, "3 points"));
    const double same[] = {1, 1, 1}, two[] = {1, 1, 2};
    CHECK(calc_fit(CALC_FIT_LINEAR, same, ys, 3, &f, &why) == -1 && contains(why, "all the same"));
    CHECK(calc_fit(CALC_FIT_QUAD, two, ys, 3, &f, &why) == -1 && contains(why, "3 different"));
    const double neg[] = {1, -1, 2};
    CHECK(calc_fit(CALC_FIT_EXP, xs, neg, 3, &f, &why) == -1 && contains(why, "above 0"));
    CHECK(calc_fit(CALC_FIT_LINEAR, xs, bad, 2, &f, &why) == -1 && contains(why, "number"));
    CHECK(calc_fit(7, xs, ys, 3, &f, &why) == -1);
}

static void test_relations_gui(void) {
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    calc_gui_init(&g);

    calc_gui_set(&g, 0, "x^2 + y^2 = 25");
    CHECK(g.ok[0] && g.kind[0] == CALC_IMPLICIT);
    calc_gui_set(&g, 1, "y < sin(x)");
    CHECK(g.ok[1] && g.kind[1] == CALC_INEQ && g.strict[1]);
    calc_gui_set(&g, 2, "x y >= 4");
    CHECK(g.ok[2] && g.kind[2] == CALC_INEQ && !g.strict[2]);
    calc_gui_set(&g, 3, " y = x^2 - 4");
    CHECK(g.ok[3] && g.kind[3] == CALC_FUNC && g.body[3] == 4);
    g.show_roots = 1;
    frame();
    CHECK(g.nroots[3] == 2 && g.nroots[0] == 0); /* roots of y4 only */
    NEAR(fabs(g.roots[3][0]), 2, 1e-9);
    sqg_rect inner = sqg_rect_inset(g.graph_at, 2);
    /* the circle at (0, 5), y < sin x shaded at (0, -5) but not at (0, 5)... */
    CHECK(count(sqg_rect_make(pxx(&g.view, inner, 0) - 3, pxy(&g.view, inner, 5) - 3, 7, 7), 0xFF2060D0u) > 0);
    uint32_t below = px[pxy(&g.view, inner, -7) * W + pxx(&g.view, inner, 2)];
    uint32_t above = px[pxy(&g.view, inner, 7) * W + pxx(&g.view, inner, -2)];
    CHECK(below != 0xFFFFFFFFu && above == 0xFFFFFFFFu);
    /* ...and x y >= 4 shaded at (5, 5) */
    CHECK(px[pxy(&g.view, inner, 5) * W + pxx(&g.view, inner, 5)] != 0xFFFFFFFFu);
    CHECK(calc_gui_eval(&g, "integrate y4 from 0 to 3") == 0 && contains(g.hist, "= -3"));
    CHECK(calc_gui_eval(&g, "integrate y1 from 0 to 3") == -1);
    /* d/dx and crossings use the function's body */
    calc_gui_set(&g, 0, "y = x^3");
    g.show_deriv = 1;
    frame();
    CHECK(g.dok && !strcmp(g.dtext, "y1' = 3*x^2"));
    calc_gui_set(&g, 1, "y = 2 x");
    calc_gui_set(&g, 2, "");
    calc_gui_set(&g, 3, "");
    g.show_cross = 1;
    frame();
    CHECK(g.ncross == 3);

    /* the other side, x alone, constants and variables */
    calc_gui_set(&g, 0, "2 > x");
    CHECK(g.ok[0] && g.kind[0] == CALC_INEQ);
    calc_gui_set(&g, 0, "x = 3");
    CHECK(g.ok[0] && g.kind[0] == CALC_IMPLICIT);
    calc_gui_set(&g, 0, "y = y^2 - x");
    CHECK(g.ok[0] && g.kind[0] == CALC_IMPLICIT);
    CHECK(calc_gui_eval(&g, "k = 2") == 0);
    calc_gui_set(&g, 0, "x^2 + y^2 <= k");
    CHECK(g.ok[0] && g.kind[0] == CALC_INEQ);
    frame();

    /* errors */
    calc_gui_set(&g, 0, "x < y < 2");
    CHECK(!g.ok[0] && contains(g.err[0], "one ="));
    calc_gui_set(&g, 0, "= 3");
    CHECK(!g.ok[0] && contains(g.err[0], "missing beside ="));
    calc_gui_set(&g, 0, "x^2 + y");
    CHECK(!g.ok[0] && contains(g.err[0], "with y, write an equation"));
    calc_gui_set(&g, 0, "x + q = y");
    CHECK(!g.ok[0] && contains(g.err[0], "define q first"));
    calc_gui_set(&g, 0, "y = x +");
    CHECK(!g.ok[0] && contains(g.err[0], "(at 8)"));
    calc_gui_set(&g, 0, "x^2 + y^2 = (1");
    CHECK(!g.ok[0] && g.err[0][0]);
    CHECK(calc_gui_eval(&g, "y = 2") == -1 && contains(g.hist, "y can't be assigned"));
    frame();
}

static void test_lists_gui(void) {
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    calc_gui_init(&g);
    CHECK(calc_gui_eval(&g, "xs = {1, 2, 3, 4, 5, 6}") == 0 && contains(g.hist, "xs = {1, 2, 3, 4, 5, 6}"));
    CHECK(calc_gui_eval(&g, "ys = [2.1, 3.9, 6.2, 7.8, 10.1, 12.2]") == 0 && g.nlists == 2 && g.llen[1] == 6);
    CHECK(calc_gui_eval(&g, "a = 2") == 0);
    CHECK(calc_gui_eval(&g, "zs = {a, a^2, sqrt(16), (1, 2)}") == -1); /* a pair is not a number */
    CHECK(calc_gui_eval(&g, "zs = {a, a^2, max(3, 4)}") == 0 && contains(g.hist, "zs = {2, 4, 4}"));
    CHECK(calc_gui_eval(&g, "stats ys") == 0 && contains(g.hist, "n = 6  mean = 7.05  sd = 3.78246") &&
          contains(g.hist, "min = 2.1  q1 = 3.9  median = 7  q3 = 10.1  max = 12.2") && contains(g.hist, "sum = 42.3"));
    NEAR(calc_gui_var(&g, "ans"), 7.05, 1e-12);
    CHECK(calc_gui_eval(&g, "linreg xs ys") == 0 && contains(g.hist, "  y = 2.02 x - 0.02") &&
          contains(g.hist, "r^2 = 0.998211  r = 0.999105"));
    CHECK(g.fit_on == 2 && g.fit.kind == CALC_FIT_LINEAR);
    NEAR(calc_gui_var(&g, "ans"), 0.9982106661074994, 1e-12);
    /* drawn: the view moves onto the points, which are ringed, and the line */
    frame();
    CHECK(g.view.xmin < 1 && g.view.xmin > 0 && g.view.xmax > 6 && g.view.xmax < 7);
    CHECK(g.view.ymin < 2.1 && g.view.ymax > 12.2 && g.view.ymax < 14);
    sqg_rect inner = sqg_rect_inset(g.graph_at, 2);
    CHECK(count(sqg_rect_make(pxx(&g.view, inner, 4) - 2, pxy(&g.view, inner, 7.8) - 2, 5, 5), 0xFF303030u) > 0);
    CHECK(count(sqg_rect_make(pxx(&g.view, inner, 5.5) - 3, pxy(&g.view, inner, 11.09) - 3, 7, 7), 0xFFE07000u) > 0);
    CHECK(contains(g.status, "Fit y = 2.02 x - 0.02, r^2 = 0.99821"));
    /* panning afterwards is not undone */
    double x0 = g.view.xmin;
    frame();
    CHECK(g.view.xmin == x0);
    CHECK(calc_gui_eval(&g, "plot xs zs") == -1 && contains(g.hist, "not the same length"));
    CHECK(calc_gui_eval(&g, "ys2 = {1, 2.7, 7.4, 20, 54.6, 148}") == 0);
    CHECK(calc_gui_eval(&g, "expreg xs ys2") == 0 && contains(g.hist, "y = 0.367102 e^(1.00002 x)"));
    CHECK(calc_gui_eval(&g, "quadreg xs ys") == 0 && contains(g.hist, "0.0267857 x^2 + 1.8325 x + 0.23"));
    CHECK(calc_gui_eval(&g, "plot xs ys2") == 0 && g.fit_on == 1);
    frame();
    CHECK(g.view.ymax > 148 && !contains(g.status, "Fit"));
    CHECK(calc_gui_eval(&g, "xs") == 0 && contains(g.hist + strlen(g.hist) - 25, "xs = {1, 2, 3, 4, 5, 6}"));

    /* errors */
    CHECK(calc_gui_eval(&g, "ws = {1, 2") == -1 && contains(g.hist, "a list ends with }"));
    CHECK(calc_gui_eval(&g, "ws = {1, q}") == -1 && contains(g.hist, "item 2 has no value"));
    CHECK(calc_gui_eval(&g, "ws = {}") == -1 && contains(g.hist, "a list needs a value"));
    CHECK(calc_gui_eval(&g, "a = {1}") == -1 && contains(g.hist, "is a number already"));
    CHECK(calc_gui_eval(&g, "xs = 3") == -1 && contains(g.hist, "xs is a list"));
    CHECK(calc_gui_eval(&g, "ws = {1}") == -1 && contains(g.hist, "no room for more lists"));
    CHECK(calc_gui_eval(&g, "xs = {7, 8}") == 0 && g.llen[0] == 2); /* replaced in place */
    CHECK(calc_gui_eval(&g, "linreg xs") == -1 && contains(g.hist, "name two lists"));
    CHECK(calc_gui_eval(&g, "linreg xs nope") == -1 && contains(g.hist, "nope is not a list"));
    CHECK(calc_gui_eval(&g, "stats") == -1 || contains(g.hist, "stats"));
    CHECK(calc_gui_eval(&g, "stats nope") == -1 && contains(g.hist, "nope is not a list or a range"));
    CHECK(calc_gui_eval(&g, "stats xs ys") == -1 && contains(g.hist, "name one list"));
    CHECK(calc_gui_eval(&g, "zs = {1, -2, 3}") == 0);
    CHECK(calc_gui_eval(&g, "expreg zs zs") == -1 && contains(g.hist, "above 0"));
    CHECK(calc_gui_eval(&g, "quadreg xs xs") == -1 && contains(g.hist, "3 points"));
    calc_gui_set(&g, 0, "xs x");
    CHECK(!g.ok[0] && contains(g.err[0], "xs is a list"));
    /* a list of 100, and one too many */
    char big[1200];
    size_t at = (size_t)snprintf(big, sizeof big, "zs = {");
    for (int i = 0; i < 101; i++) at += (size_t)snprintf(big + at, sizeof big - at, "%s%d", i ? "," : "", i);
    snprintf(big + at, sizeof big - at, "}");
    CHECK(calc_gui_eval(&g, big) == -1 && contains(g.hist, "holds 100"));
    char *last = strrchr(big, ',');
    last[0] = '}';
    last[1] = 0;
    CHECK(calc_gui_eval(&g, big) == 0 && g.llen[2] == 100 && contains(g.hist, "(100 items)"));
}

static void test_cas_step3(void) {
    calc_gui_init(&g);
    CHECK(calc_gui_eval(&g, "factor x^3 - x") == 0 && contains(g.hist, "factor x^3 - x = x*(x - 1)*(x + 1)"));
    CHECK(calc_gui_eval(&g, "apart 1/(x^2 - 1)") == 0 && contains(g.hist, "= 1/(2*(x - 1)) - 1/(2*(x + 1))"));
    CHECK(calc_gui_eval(&g, "integrate x e^x") == 0 && contains(g.hist, "integrate x e^x = e^x*(x - 1) + C"));
    calc_gui_set(&g, 0, "y = cos(x)");
    CHECK(calc_gui_eval(&g, "integrate y1") == 0 && contains(g.hist, "integrate y1 = sin(x) + C"));
    CHECK(calc_gui_eval(&g, "integrate e^(x^2)") == -1 && contains(g.hist, "no exact integral found"));
    calc_gui_set(&g, 1, "x^2 + y^2 = 1");
    CHECK(calc_gui_eval(&g, "integrate y2") == -1 && contains(g.hist, "not a function of x"));
    CHECK(calc_gui_eval(&g, "factor x + q") == -1 && contains(g.hist, "no exact form found"));
    CHECK(calc_gui_eval(&g, "integrate y1 from 0 to pi/2") == 0 && contains(g.hist, "= 1")); /* still numeric */
}

/* the centre of cell (r, c) of the sheet as last drawn */
static void cell_xy(int r, int c, int *x, int *y) {
    sqg_rect a = g.sheet_at;
    int cw = (a.w - 30) / CALC_SHEET_COLS;
    *x = a.x + 30 + c * cw + cw / 2;
    *y = a.y + 16 + (r - g.sheet_top) * 16 + 8;
}

static void type(const char *text) {
    snprintf(in.text, sizeof in.text, "%s", text);
    frame();
}

static void key(int k) {
    in.key = k;
    frame();
}

static void test_sheet_gui(const char *dir) {
    calc_gui_init(&g);
    /* the calculator line sets cells and reads them */
    CHECK(calc_gui_eval(&g, "A1 = 3") == 0 && contains(g.hist, "A1 = 3"));
    CHECK(calc_gui_eval(&g, "A2 = A1 * 2") == 0 && !strcmp(g.sheet.text[1][0], "=A1 * 2") && contains(g.hist, "A2 = 6"));
    CHECK(calc_gui_eval(&g, "A3 = \"total\"") == 0 && g.sheet.kind[2][0] == CALC_CELL_TEXT);
    CHECK(calc_gui_eval(&g, "A1 + A2") == 0 && contains(g.hist, "A1 + A2 = 9"));
    CHECK(calc_gui_eval(&g, "A3 + 1") == -1 && contains(g.hist, "A3 is text"));
    CHECK(calc_gui_eval(&g, "B1 = B1 + 1") == -1 && contains(g.hist, "cycle"));
    CHECK(calc_gui_eval(&g, "B1 =") == 0 && g.sheet.kind[0][1] == CALC_CELL_EMPTY);
    /* cells follow the calculator's variables and lists */
    CHECK(calc_gui_eval(&g, "k = 10") == 0 && calc_gui_eval(&g, "B2 = k + A1") == 0);
    NEAR(g.sheet.val[1][1], 13, 0);
    CHECK(calc_gui_eval(&g, "k = 20") == 0);
    NEAR(g.sheet.val[1][1], 23, 0);
    CHECK(calc_gui_eval(&g, "xs = {1, 2, 3, 4}") == 0 && calc_gui_eval(&g, "B3 = mean(xs)") == 0);
    NEAR(g.sheet.val[2][1], 2.5, 0);
    CHECK(calc_gui_eval(&g, "xs = {10, 20}") == 0);
    NEAR(g.sheet.val[2][1], 15, 0);
    /* fill: down a column with the names moved */
    CHECK(calc_gui_eval(&g, "C1 = 1") == 0 && calc_gui_eval(&g, "C2 = C1 + 1") == 0);
    CHECK(calc_gui_eval(&g, "fill C2:C10") == 0 && !strcmp(g.sheet.text[9][2], "=C9 + 1"));
    NEAR(g.sheet.val[9][2], 10, 0);
    CHECK(calc_gui_eval(&g, "fill C2:D9") == -1 && contains(g.hist, "fill a column or a row"));
    CHECK(calc_gui_eval(&g, "fill C2") == -1);
    /* ranges wherever a list goes */
    CHECK(calc_gui_eval(&g, "stats C1:C10") == 0 && contains(g.hist, "n = 10  mean = 5.5"));
    CHECK(calc_gui_eval(&g, "D1 = 2") == 0 && calc_gui_eval(&g, "D2 = D1 + 2") == 0 && calc_gui_eval(&g, "fill D2:D10") == 0);
    CHECK(calc_gui_eval(&g, "linreg C1:C10 D:D") == 0 && contains(g.hist, "y = 2 x"));
    CHECK(g.fit_on == 2 && g.fit_n == 10 && g.fit_y[9] == 20);
    CHECK(calc_gui_eval(&g, "linreg C1:C10 D1:D5") == -1 && contains(g.hist, "not the same length"));
    CHECK(calc_gui_eval(&g, "stats E:E") == -1 && contains(g.hist, "E:E has no numbers"));
    CHECK(calc_gui_eval(&g, "sum(C:C) + normalcdf(-1e99, 0)") == 0 && contains(g.hist, "= 55.5"));
    /* a graph row may use cells: it is redrawn when they change */
    calc_gui_set(&g, 0, "A1 x");
    CHECK(g.ok[0]);
    double values[CALC_VARS + 1] = {2};
    for (int i = 0; i < g.nvars; i++) values[i + 1] = g.vval[i];
    NEAR(sqm_run(&g.prog[0], values), 6, 0);
    CHECK(calc_gui_eval(&g, "A1 = 5") == 0);
    NEAR(sqm_run(&g.prog[0], values), 10, 0);
    calc_gui_set(&g, 1, "y = normalpdf(x, 0, 2)");
    CHECK(g.ok[1] && g.kind[1] == CALC_FUNC);
    values[0] = 0;
    NEAR(sqm_run(&g.prog[1], values), 0.19947114020071635, 1e-15);
    calc_gui_set(&g, 2, "A3 x");
    CHECK(!g.ok[2] && contains(g.err[2], "A3 is text"));
    calc_gui_set(&g, 2, "");
    CHECK(calc_gui_eval(&g, "a1 = 4") == 0 && !isnan(calc_gui_var(&g, "a1"))); /* lower case is a variable */
    CHECK(calc_gui_cell(&g, "I1", "1") == -1 && calc_gui_cell(&g, "A1x", "1") == -1 && calc_gui_cell(&g, "A1", "3") == 0);

    /* hist and box draw on the graph and move the view onto it */
    CHECK(calc_gui_eval(&g, "hs = {1, 2, 2, 3, 3, 3, 4, 4, 5, 9}") == 0);
    CHECK(calc_gui_eval(&g, "hist hs") == 0 && contains(g.hist, "5 bins of 2 from 0") && g.stat_plot == CALC_PLOT_HIST);
    frame();
    CHECK(g.view.xmin < 0 && g.view.xmax > 10 && g.view.ymax > 5 && g.view.ymax < 6);
    CHECK(count(g.graph_at, 0xFF305080u) > 100); /* the bar edges */
    CHECK(calc_gui_eval(&g, "hist hs 0.5") == 0 && contains(g.hist, "16 bins of 0.5 from 1") && g.hist_bins == 16);
    CHECK(calc_gui_eval(&g, "hist hs -1") == -1 && contains(g.hist, "above 0"));
    CHECK(calc_gui_eval(&g, "box hs") == 0 && contains(g.hist, "box hs: 1 | 2 [3] 4 | 9") && g.stat_plot == CALC_PLOT_BOX);
    frame();
    CHECK(count(g.graph_at, 0xFF305080u) > 100);
    CHECK(calc_gui_eval(&g, "box hs hs") == -1);

    /* the Sheet view: click a cell, type, Enter goes down a row */
    g.show_sheet = 1;
    frame();
    CHECK(g.sheet_at.w > 200 && g.sheet_at.h > 100);
    int x, y;
    cell_xy(4, 1, &x, &y); /* B5 */
    click(x, y);
    CHECK(g.sel_r == 4 && g.sel_c == 1 && ui.focus == sqg_id_str("cell"));
    type("=sum(");
    type("C1:C4)");
    key(SQG_KEY_ENTER);
    CHECK(!strcmp(g.sheet.text[4][1], "=sum(C1:C4)") && g.sel_r == 5);
    NEAR(g.sheet.val[4][1], 10, 0);
    type("7");
    key(SQG_KEY_UP); /* moving also puts the line in */
    CHECK(g.sheet.val[5][1] == 7 && g.sel_r == 4 && !strcmp(g.cell_line, "=sum(C1:C4)"));
    key(SQG_KEY_TAB);
    CHECK(g.sel_c == 2 && g.sel_r == 4 && !strcmp(g.cell_line, "=C4 + 1"));
    key(SQG_KEY_DOWN);
    CHECK(g.sel_r == 5);
    /* the values are drawn: a formula's in blue, an error as #ERR */
    calc_gui_cell(&g, "E1", "=nope");
    frame();
    cell_xy(0, 4, &x, &y);
    CHECK(count(sqg_rect_make(x - 30, y - 7, 60, 14), 0xFFB02020u) > 5);
    cell_xy(4, 1, &x, &y);
    CHECK(count(sqg_rect_make(x - 30, y - 7, 60, 14), 0xFF2050A0u) > 5);
    /* the wheel scrolls, and selecting far down keeps the cell in view */
    in.mx = g.sheet_at.x + 100;
    in.my = g.sheet_at.y + 100;
    in.wheel = -5;
    frame();
    CHECK(g.sheet_top == 15);
    g.sel_r = 49;
    frame();
    CHECK(g.sheet_top + (g.sheet_at.h - 16) / 16 > 49);
    if (dir) {
        char path[512];
        snprintf(path, sizeof path, "%s/calc_sheet.ppm", dir);
        CHECK(sqg_write_ppm(&cv, path) == 0);
    }
    g.show_sheet = 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    test_steps();
    test_view();
    test_plot();
    test_gui(dir);
    test_analysis();
    test_curves();
    test_implicit();
    test_stats();
    test_relations_gui();
    test_lists_gui();
    test_cas_step3();
    test_sheet_gui(dir);
    printf("test_calc: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}

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

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    test_steps();
    test_view();
    test_plot();
    test_gui(dir);
    test_analysis();
    printf("test_calc: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}

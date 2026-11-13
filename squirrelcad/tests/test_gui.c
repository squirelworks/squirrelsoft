/* SquirrelCAD window panel, driven off screen: clicks draw, snaps, ortho,
 * zoom and pan, selection and erase, undo, the command line. Usage:
 * test_gui BUILD_DIR (writes BUILD_DIR/cad_gui.ppm) */
#include "sqc_gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
#define NEAR(a, b, tol) check(fabs((a) - (b)) <= (tol), #a " ~ " #b, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL test_gui.c line %d: %s\n", line, what);
    }
}

#define W 800
#define H 600
static uint32_t px[W * H];
static sqg_canvas cv;
static sqg_ui ui;
static sqg_input in;
static SqcSession S;
static SqcGui g;

static void frame(void) {
    sqg_begin(&ui, &cv, &in);
    sqc_gui_frame(&g, &ui);
    sqg_end(&ui);
    in.pressed = in.released = 0;
    in.wheel = 0;
    in.key = 0;
    in.text[0] = 0;
}

static void move(double x, double y) {
    in.mx = (int)lround(x);
    in.my = (int)lround(y);
    frame();
}

static void click_px(double x, double y) {
    move(x, y);
    in.down = in.pressed = 1;
    frame();
    in.down = 0;
    in.released = 1;
    frame();
}

/* a click at a drawing point, nudged by some pixels */
static void click_at(double x, double y, int dx, int dy) {
    double sx, sy;
    sqc_gui_to_screen(&g, (SqcPoint){x, y, 0}, &sx, &sy);
    click_px(sx + dx, sy + dy);
}

static void key(int k) {
    in.key = k;
    frame();
}

static int count(sqg_rect r, uint32_t col) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) n += px[y * W + x] == col;
    return n;
}

/* the middle of the n-th toolbar button */
static void toolbar(const char *const *labels, int n) {
    int x = 4;
    for (int i = 0; i < n; i++) x += sqg_button_w(&ui, labels[i]) + 4;
    click_px(x + sqg_button_w(&ui, labels[n]) / 2, 4 + sqg_line_h(&ui) / 2);
}
static const char *const BAR[] = {"Select", "Line", "Circle", "PLine"};

static const SqcEntity *last_ent(void) { return S.dwg.ent_count ? &S.dwg.ents[S.dwg.ent_count - 1] : NULL; }

static int same(SqcPoint p, double x, double y) { return fabs(p.x - x) < 1e-9 && fabs(p.y - y) < 1e-9; }

static void test_draw(void) {
    g.ppu = 6; /* x from -60 to 60 or so */
    g.grid_on = 1;
    toolbar(BAR, 1); /* Line */
    CHECK(g.tool == SQC_TOOL_LINE);
    click_at(0, 0, 2, -1); /* to the grid */
    click_at(10, 0, 1, 1);
    click_at(10, 5, -2, 2);
    CHECK(S.dwg.ent_count == 2);
    CHECK(S.dwg.ents[0].type == SQC_ENT_LINE && same(S.dwg.ents[0].p1, 0, 0) && same(S.dwg.ents[0].p2, 10, 0));
    CHECK(same(S.dwg.ents[1].p1, 10, 0) && same(S.dwg.ents[1].p2, 10, 5)); /* chained */
    key(SQG_KEY_ESCAPE);
    CHECK(g.npts == 0 && g.tool == SQC_TOOL_LINE);

    /* off the grid, a click near an end takes the end exactly */
    g.grid_on = 0;
    click_at(10, 5, 4, 3);
    CHECK(g.snap_kind == SQC_SNAP_END && g.npts == 1 && same(g.pts[0], 10, 5));
    SqcPoint m;
    double mx, my;
    sqc_gui_to_screen(&g, (SqcPoint){5, 0, 0}, &mx, &my);
    CHECK(sqc_gui_snap(&g, mx + 3, my - 2, 8, &m) == SQC_SNAP_MID && same(m, 5, 0));
    click_at(5, 0, 3, -2); /* the midpoint of the first line */
    CHECK(same(last_ent()->p1, 10, 5) && same(last_ent()->p2, 5, 0));
    key(SQG_KEY_ESCAPE);

    /* ortho: the line keeps to the nearer axis */
    g.grid_on = g.ortho = 1;
    click_at(0, -10, 0, 0);
    click_at(7, -9, 0, 0);
    CHECK(same(last_ent()->p1, 0, -10) && same(last_ent()->p2, 7, -10));
    click_at(8, -3, 0, 0);
    CHECK(same(last_ent()->p1, 7, -10) && same(last_ent()->p2, 7, -3));
    key(SQG_KEY_ESCAPE);
    g.ortho = 0;

    /* a circle by centre and a point on it */
    toolbar(BAR, 2);
    click_at(30, 0, 0, 0);
    click_at(33, 4, 0, 0);
    CHECK(last_ent()->type == SQC_ENT_CIRCLE && same(last_ent()->p1, 30, 0));
    NEAR(last_ent()->radius, 5, 1e-9);
    /* its centre and quadrants snap */
    g.grid_on = 0;
    SqcPoint p;
    double sx, sy;
    sqc_gui_to_screen(&g, (SqcPoint){30, 0, 0}, &sx, &sy);
    CHECK(sqc_gui_snap(&g, sx + 3, sy + 2, 8, &p) == SQC_SNAP_CEN && same(p, 30, 0));
    sqc_gui_to_screen(&g, (SqcPoint){30, 5, 0}, &sx, &sy);
    CHECK(sqc_gui_snap(&g, sx - 2, sy + 3, 8, &p) == SQC_SNAP_END && same(p, 30, 5));
    g.grid_on = 1;

    /* a polyline closed by clicking its first point, and an open one ended by Enter */
    toolbar(BAR, 3);
    int before = S.dwg.ent_count;
    click_at(40, 0, 0, 0);
    click_at(50, 0, 0, 0);
    click_at(50, 10, 0, 0);
    CHECK(S.dwg.ent_count == before);
    click_at(40, 0, 3, 3);
    CHECK(S.dwg.ent_count == before + 1);
    const SqcEntity *e = last_ent();
    CHECK(e->type == SQC_ENT_PLINE && e->vert_count == 4 && same(e->verts[3], 40, 0));
    click_at(40, 20, 0, 0);
    click_at(45, 25, 0, 0);
    click_at(50, 20, 0, 0);
    key(SQG_KEY_ENTER);
    e = last_ent();
    CHECK(S.dwg.ent_count == before + 2 && e->vert_count == 3 && same(e->verts[2], 50, 20));
    /* Esc leaves a half-drawn one; a second Esc goes back to Select */
    click_at(60, 0, 0, 0);
    key(SQG_KEY_ESCAPE);
    CHECK(g.npts == 0 && g.tool == SQC_TOOL_PLINE && S.dwg.ent_count == before + 2);
    key(SQG_KEY_ESCAPE);
    CHECK(g.tool == SQC_TOOL_SELECT);
}

static void test_view(void) {
    /* the wheel zooms about the pointer */
    move(300, 250);
    SqcPoint a = sqc_gui_to_world(&g, 300, 250);
    double ppu = g.ppu;
    in.wheel = 2;
    frame();
    SqcPoint b = sqc_gui_to_world(&g, 300, 250);
    NEAR(g.ppu, ppu * 1.5625, 1e-9 * ppu);
    NEAR(a.x, b.x, 1e-9);
    NEAR(a.y, b.y, 1e-9);
    in.wheel = -2;
    frame();
    NEAR(g.ppu, ppu, 1e-9 * ppu);

    /* dragging empty space pans: the point grabbed follows the pointer */
    sqc_gui_fit(&g);
    double sx, sy;
    sx = 120, sy = 450; /* nothing there */
    move(sx, sy);
    CHECK(sqc_gui_pick(&g, sx, sy, 5) == 0);
    SqcPoint grab = sqc_gui_to_world(&g, sx, sy);
    in.down = in.pressed = 1;
    frame();
    CHECK(g.dragging && g.selected == 0);
    move(sx + 60, sy + 25);
    move(sx + 90, sy - 30);
    in.down = 0;
    in.released = 1;
    frame();
    CHECK(!g.dragging);
    SqcPoint now = sqc_gui_to_world(&g, sx + 90, sy - 30);
    NEAR(now.x, grab.x, 1e-9);
    NEAR(now.y, grab.y, 1e-9);

    /* Fit brings everything back on screen */
    sqc_gui_fit(&g);
    SqcPoint lo, hi;
    CHECK(sqc_extents(&S.dwg, &lo, &hi));
    sqc_gui_to_screen(&g, lo, &sx, &sy);
    CHECK(sqg_rect_contains(g.area, (int)sx, (int)sy));
    sqc_gui_to_screen(&g, hi, &sx, &sy);
    CHECK(sqg_rect_contains(g.area, (int)sx, (int)sy));
}

static void test_select(void) {
    int n = S.dwg.ent_count, id = S.dwg.ents[0].id; /* the line 0,0 - 10,0 */
    g.tool = SQC_TOOL_SELECT;
    click_at(3, 0, 0, 2);
    CHECK(g.selected == id && !g.dragging);
    move(5, 5);
    /* the selection is drawn in blue */
    CHECK(count(g.area, SQG_RGB(80, 160, 255)) > 20);
    key(SQG_KEY_DELETE);
    CHECK(S.dwg.ent_count == n - 1 && !sqc_find(&S.dwg, id) && g.selected == 0);
    /* Undo puts it back */
    int x = 4;
    for (int i = 0; i < 4; i++) x += sqg_button_w(&ui, BAR[i]) + 4;
    x += 8 + 4;
    const char *const mid[] = {"Snap", "Grid", "Ortho"};
    for (int i = 0; i < 3; i++) x += sqg_button_w(&ui, mid[i]) + 4;
    x += 8 + 4;
    x += sqg_button_w(&ui, "Fit") + 4;
    click_px(x + sqg_button_w(&ui, "Undo") / 2, 4 + sqg_line_h(&ui) / 2);
    CHECK(S.dwg.ent_count == n && sqc_find(&S.dwg, id));
    /* Erase on the selected circle */
    click_at(30, 5, 1, 0);
    const SqcEntity *e = sqc_find(&S.dwg, g.selected);
    CHECK(e && e->type == SQC_ENT_CIRCLE);
    x += sqg_button_w(&ui, "Undo") + 4;
    click_px(x + sqg_button_w(&ui, "Erase") / 2, 4 + sqg_line_h(&ui) / 2);
    CHECK(S.dwg.ent_count == n - 1 && g.selected == 0);
    /* a click on nothing selects nothing */
    click_px(120, 450);
    CHECK(g.selected == 0);

    /* a block insert is picked through its parts */
    CHECK(sqc_gui_exec(&g, "LINE 100,100 104,100") == SQC_CMD_OK);
    int part = g.s->last_id;
    char cmd[64];
    snprintf(cmd, sizeof cmd, "BLOCK B 100,100 %d", part);
    CHECK(sqc_gui_exec(&g, cmd) == SQC_CMD_OK);
    CHECK(sqc_gui_exec(&g, "INSERT B 0,30 2 90") == SQC_CMD_OK);
    int ins = g.s->last_id;
    sqc_gui_fit(&g);
    double sx, sy;
    sqc_gui_to_screen(&g, (SqcPoint){0, 34, 0}, &sx, &sy); /* the rotated, scaled line */
    CHECK(sqc_gui_pick(&g, sx + 1, sy, 5) == ins);
    sqc_gui_to_screen(&g, (SqcPoint){102, 100, 0}, &sx, &sy); /* the definition: not drawn */
    CHECK(sqc_gui_pick(&g, sx, sy, 5) != part);
}

static void test_command_line(void) {
    int n = S.dwg.ent_count;
    /* click into the command line, type, Enter */
    int y = H - 4 - sqg_line_h(&ui) - 4 - sqg_line_h(&ui) / 2;
    click_px(W / 2, y);
    CHECK(ui.focus != 0);
    snprintf(in.text, sizeof in.text, "circle 0,0 2");
    frame();
    CHECK(strcmp(g.line, "circle 0,0 2") == 0);
    key(SQG_KEY_ENTER);
    CHECK(S.dwg.ent_count == n + 1 && last_ent()->type == SQC_ENT_CIRCLE && g.line[0] == 0);
    CHECK(strstr(g.out, "id=") != NULL);
    /* keys for the drawing do not fire while typing */
    g.tool = SQC_TOOL_LINE;
    key(SQG_KEY_ESCAPE);
    CHECK(g.tool == SQC_TOOL_LINE);
    /* an error is shown, nothing changes */
    snprintf(in.text, sizeof in.text, "bogus");
    frame();
    key(SQG_KEY_ENTER);
    CHECK(S.dwg.ent_count == n + 1 && g.out[0]);
    /* QUIT asks the window to close */
    snprintf(in.text, sizeof in.text, "quit");
    frame();
    key(SQG_KEY_ENTER);
    CHECK(ui.quit == 1);
    ui.quit = 0;
}

int main(int argc, char **argv) {
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    sqc_session_init(&S, "");
    sqc_gui_init(&g, &S);
    in.mx = in.my = -1;
    frame();
    CHECK(g.view_ready && g.area.w > 0 && g.area.h > 0);
    /* an empty drawing: black, the origin's axes, no entities */
    CHECK(count(g.area, SQG_RGB(0, 0, 0)) > g.area.w * g.area.h * 9 / 10);
    CHECK(count(g.area, SQG_RGB(255, 60, 60)) >= 20);

    test_draw();
    test_view();
    test_select();
    test_command_line();

    /* everything is drawn in its layer's colour on black */
    g.selected = 0;
    sqc_gui_fit(&g);
    move(-1, -1);
    CHECK(count(g.area, SQG_RGB(255, 255, 255)) > 200);
    char path[512];
    snprintf(path, sizeof path, "%s/cad_gui.ppm", argc > 1 ? argv[1] : ".");
    CHECK(sqg_write_ppm(&cv, path) == 0);

    printf("test_gui: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

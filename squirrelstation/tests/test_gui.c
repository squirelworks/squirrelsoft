/* SquirrelStation window panel, driven off screen: views, placing with the
 * mouse and keyins, AccuSnap and AccuDraw, selection, undo, save. Usage:
 * test_gui BUILD_DIR (writes BUILD_DIR/station_gui.ppm and .sqdgnb) */
#include "sqs_gui.h"

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
static SqsDesign D, back;
static SqsGui g;

static void frame(void) {
    sqg_begin(&ui, &cv, &in);
    sqs_gui_frame(&g, &ui);
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

static void screen(SqsPoint p, double *x, double *y) { sqs_gui_to_screen(&g, p, x, y); }

static void click_at(SqsPoint p, int dx, int dy) {
    double x, y;
    screen(p, &x, &y);
    click_px(x + dx, y + dy);
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

/* toolbar buttons by label, laid out as the panel does */
static const char *const BAR[] = {"Select", "Line", "Circle", "|", "Top", "Front", "Right", "Iso", "|",
                                  "Snap",   "Ortho", "|",     "Fit", "Undo", "Delete", "Save"};
static void button(const char *label) {
    int x = 4;
    for (size_t i = 0; i < sizeof BAR / sizeof BAR[0]; i++) {
        if (!strcmp(BAR[i], "|")) {
            x += 8 + 4;
            continue;
        }
        int w = sqg_button_w(&ui, BAR[i]);
        if (!strcmp(BAR[i], label)) {
            click_px(x + w / 2, 4 + sqg_line_h(&ui) / 2);
            return;
        }
        x += w + 4;
    }
    check(0, label, __LINE__);
}

static void type(const char *text) {
    click_px(W / 2, H - 4 - sqg_line_h(&ui) - 4 - sqg_line_h(&ui) / 2);
    snprintf(in.text, sizeof in.text, "%s", text);
    frame();
    key(SQG_KEY_ENTER);
}

static const SqsElement *last_el(void) { return D.element_count ? &D.elements[D.element_count - 1] : NULL; }
static int same(SqsPoint p, double x, double y, double z) {
    return fabs(p.x - x) < 1e-9 && fabs(p.y - y) < 1e-9 && fabs(p.z - z) < 1e-9;
}

static void test_projection(void) {
    const SqsPoint p = {3, -4, 5};
    for (int v = 0; v < SQS_VIEWS; v++) {
        g.view = v;
        double x, y;
        screen(p, &x, &y);
        SqsPoint q = sqs_gui_to_world(&g, x, y, p); /* on the plane through p: p itself */
        CHECK(sqs_dist(p, q) < 1e-9);
    }
    double x, y;
    g.view = SQS_VIEW_FRONT;
    screen(p, &x, &y);
    SqsPoint q = sqs_gui_to_world(&g, x, y, (SqsPoint){0, 7, 0});
    CHECK(same(q, 3, 7, 5)); /* depth from the plane, x and z from the screen */
    g.view = SQS_VIEW_RIGHT;
    screen(p, &x, &y);
    q = sqs_gui_to_world(&g, x, y, (SqsPoint){-2, 0, 0});
    CHECK(same(q, -2, -4, 5));
    g.view = SQS_VIEW_ISO; /* Iso puts free points on the horizontal plane */
    screen((SqsPoint){1, 2, 0}, &x, &y);
    q = sqs_gui_to_world(&g, x, y, (SqsPoint){0, 0, 0});
    CHECK(same(q, 1, 2, 0));
    /* up on screen is +z in Front, Right and Iso; +y in Top */
    double y0, y1;
    screen((SqsPoint){0, 0, 0}, &x, &y0);
    screen((SqsPoint){0, 0, 1}, &x, &y1);
    CHECK(y1 < y0);
    g.view = SQS_VIEW_TOP;
}

static void test_place(void) {
    g.ppu = 8;
    g.cx = g.cy = 0;
    button("Line");
    CHECK(g.tool == SQS_TOOL_LINE);
    type("xy=0,0"); /* a click is only as exact as its pixel */
    CHECK(g.npts == 1 && same(g.last, 0, 0, 0));
    /* the compass is on the first point */
    move(300, 300);
    CHECK(count(g.area, SQG_RGB(0, 200, 200)) > 40);
    /* AccuDraw indexing: a few pixels off the x axis lands on it */
    click_at((SqsPoint){20, 0, 0}, 0, -3);
    const SqsElement *e = last_el();
    CHECK(D.element_count == 1 && e->type == SQS_LINE && same(e->a, 0, 0, 0) && same(e->b, 20, 0, 0));
    /* the chain goes on; keyins are from the last point */
    type("dl=0,10");
    CHECK(D.element_count == 2 && same(last_el()->a, 20, 0, 0) && same(last_el()->b, 20, 10, 0));
    type("di=10,180");
    CHECK(same(last_el()->b, 10, 10, 0));
    type("xy=10,20,0");
    CHECK(same(last_el()->a, 10, 10, 0) && same(last_el()->b, 10, 20, 0));
    CHECK(ui.focus != 0);
    key(SQG_KEY_ESCAPE); /* from the empty keyin line */
    CHECK(g.npts == 0 && g.tool == SQS_TOOL_LINE);
    key(SQG_KEY_ESCAPE);
    CHECK(g.tool == SQS_TOOL_SELECT);
    /* but not while something is typed there */
    button("Line");
    type("xy=1,1");
    click_px(W / 2, H - 4 - sqg_line_h(&ui) - 4 - sqg_line_h(&ui) / 2);
    snprintf(in.text, sizeof in.text, "dl");
    frame();
    key(SQG_KEY_ESCAPE);
    CHECK(g.npts == 1 && !strcmp(g.keyin, "dl"));
    g.keyin[0] = 0;
    key(SQG_KEY_ESCAPE);
    key(SQG_KEY_ESCAPE);
    CHECK(g.npts == 0 && g.tool == SQS_TOOL_SELECT);

    /* AccuSnap in Top: an end, a midpoint */
    button("Line");
    click_at((SqsPoint){20, 10, 0}, 4, 3);
    CHECK(g.npts == 1 && same(g.last, 20, 10, 0));
    double x, y;
    screen((SqsPoint){10, 0, 0}, &x, &y);
    SqsPoint p;
    CHECK(sqs_gui_locate(&g, x + 2, y - 3, &p) == SQS_SNAP_MIDPOINT && same(p, 10, 0, 0));
    /* Ortho locks to the nearer compass axis */
    g.snap_on = 0;
    g.ortho = 1;
    screen((SqsPoint){27, 13, 0}, &x, &y);
    CHECK(sqs_gui_locate(&g, x, y, &p) == SQS_PICK_ORTHO && fabs(p.y - 10) < 1e-9 && fabs(p.x - 27) < 0.2);
    screen((SqsPoint){22, 30, 0}, &x, &y);
    CHECK(sqs_gui_locate(&g, x, y, &p) == SQS_PICK_ORTHO && fabs(p.x - 20) < 1e-9);
    g.ortho = 0;
    g.snap_on = 1;
    key(SQG_KEY_ESCAPE);

    /* a circle from keyins, on another level */
    CHECK(sqs_add_level(&D, "Steel", 0x4080FF) > 0);
    type("lv=Steel");
    CHECK(g.level_id == sqs_level_id(&D, "Steel"));
    type("circle");
    CHECK(g.tool == SQS_TOOL_CIRCLE);
    type("xy=50,20");
    type("dl=3,4");
    e = last_el();
    CHECK(e->type == SQS_ARC && same(e->a, 50, 20, 0) && fabs(e->radius - 5) < 1e-12 && e->sweep_deg == 360);
    CHECK(e->level_id == sqs_level_id(&D, "Steel"));
    CHECK(g.npts == 0);
    type("lv=Nope");
    CHECK(strstr(g.out, "no level") != NULL);
    type("bogus words");
    CHECK(strstr(g.out, "unknown") != NULL);
    type("select");
    CHECK(g.tool == SQS_TOOL_SELECT);
}

static void test_views(void) {
    /* a box, seen from the front: its corners snap on screen, in 3-D */
    int box = sqs_add_solid(&D, 1, SQS_SOLID_BOX, (SqsPoint){30, 5, 0}, (SqsPoint){6, 4, 8});
    CHECK(box > 0);
    button("Front");
    CHECK(g.view == SQS_VIEW_FRONT);
    double x, y;
    screen((SqsPoint){36, 5, 8}, &x, &y);
    SqsPoint p;
    int k = sqs_gui_locate(&g, x + 3, y + 2, &p);
    CHECK(k == SQS_SNAP_KEYPOINT && fabs(p.x - 36) < 1e-9 && fabs(p.z - 8) < 1e-9);
    /* pick it, and say what it is */
    move(x - 20, y);
    CHECK(sqs_gui_pick(&g, x - 20, y, 5) == box);
    char info[256];
    sqs_gui_describe(&g, box, info, sizeof info);
    CHECK(strstr(info, "Solid") && strstr(info, "volume 192.0000"));

    /* the line tool in Iso: free points on the horizontal plane through the last one */
    button("Iso");
    CHECK(g.view == SQS_VIEW_ISO);
    /* the box's top corner is there to snap to */
    screen((SqsPoint){36, 9, 8}, &x, &y);
    CHECK(sqs_gui_locate(&g, x + 1, y - 2, &p) == SQS_SNAP_KEYPOINT && same(p, 36, 9, 8));
    button("Line");
    click_at((SqsPoint){36, 9, 8}, 1, -2);
    CHECK(g.npts == 1 && same(g.last, 36, 9, 8));
    g.snap_on = 0;
    click_at((SqsPoint){46, 9, 8}, 0, 0);
    const SqsElement *e = last_el();
    CHECK(e->type == SQS_LINE && same(e->a, 36, 9, 8) && fabs(e->b.z - 8) < 1e-9 && fabs(e->b.y - 9) < 1e-6);
    g.snap_on = 1;
    key(SQG_KEY_ESCAPE);
    key(SQG_KEY_ESCAPE);
    /* the view's words */
    type("right");
    CHECK(g.view == SQS_VIEW_RIGHT);
    type("TOP");
    CHECK(g.view == SQS_VIEW_TOP);
}

static void test_select(void) {
    button("Fit");
    int n = D.element_count, first = D.elements[0].id;
    double x, y;
    screen((SqsPoint){5, 0, 0}, &x, &y);
    click_px(x, y + 2);
    CHECK(g.selected == first && !g.dragging);
    move(5, 5);
    CHECK(count(g.area, SQG_RGB(80, 160, 255)) > 20);
    char info[256];
    sqs_gui_describe(&g, first, info, sizeof info);
    CHECK(strstr(info, "Line #") && strstr(info, "length 20.0000"));
    key(SQG_KEY_DELETE);
    CHECK(D.element_count == n - 1 && !sqs_find(&D, first) && g.selected == 0);
    button("Undo");
    CHECK(D.element_count == n && sqs_find(&D, first));
    button("Undo");
    CHECK(strstr(g.out, "nothing") != NULL);

    /* a placed cell is picked whole; its definition is not drawn */
    int part = sqs_add_line(&D, 1, (SqsPoint){100, 100, 0}, (SqsPoint){104, 100, 0});
    CHECK(sqs_define_cell(&D, "VALVE", (SqsPoint){100, 100, 0}, &part, 1) >= 0);
    int at = sqs_place_cell(&D, 1, "VALVE", (SqsPoint){0, 30, 0}, 2, 90);
    CHECK(at > 0);
    button("Fit");
    screen((SqsPoint){0, 34, 0}, &x, &y);
    CHECK(sqs_gui_pick(&g, x + 1, y, 5) == at);
    sqs_gui_describe(&g, at, info, sizeof info);
    CHECK(strstr(info, "Cell") && strstr(info, "VALVE"));
    screen((SqsPoint){102, 100, 0}, &x, &y);
    CHECK(sqs_gui_pick(&g, x, y, 5) != part);
    /* Delete through the button */
    g.selected = at;
    button("Delete");
    CHECK(!sqs_find(&D, at));
    button("Undo");
    CHECK(sqs_find(&D, at) != NULL);
}

static void test_view_moves(void) {
    /* the wheel zooms about the pointer */
    move(300, 250);
    SqsPoint a = sqs_gui_to_world(&g, 300, 250, (SqsPoint){0, 0, 0});
    double ppu = g.ppu;
    in.wheel = 3;
    frame();
    SqsPoint b = sqs_gui_to_world(&g, 300, 250, (SqsPoint){0, 0, 0});
    NEAR(g.ppu, ppu * 1.953125, 1e-9 * ppu);
    CHECK(sqs_dist(a, b) < 1e-9);
    /* dragging empty space pans */
    sqs_gui_fit(&g);
    double sx = 60, sy = 60;
    CHECK(sqs_gui_pick(&g, sx, sy, 5) == 0);
    move(sx, sy);
    SqsPoint grab = sqs_gui_to_world(&g, sx, sy, (SqsPoint){0, 0, 0});
    in.down = in.pressed = 1;
    frame();
    CHECK(g.dragging);
    move(sx + 70, sy + 40);
    in.down = 0;
    in.released = 1;
    frame();
    SqsPoint now = sqs_gui_to_world(&g, sx + 70, sy + 40, (SqsPoint){0, 0, 0});
    CHECK(sqs_dist(grab, now) < 1e-9);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512];
    snprintf(path, sizeof path, "%s/station_gui.sqdgnb", dir);
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    sqs_design_init(&D, "gui");
    sqs_gui_init(&g, &D, path);
    in.mx = in.my = -1;
    frame();
    CHECK(g.view_ready && g.area.w > 0 && g.view == SQS_VIEW_TOP && g.level_id == D.levels[0].id);
    CHECK(count(g.area, SQG_RGB(0, 0, 0)) > g.area.w * g.area.h * 9 / 10);

    test_projection();
    test_place();
    test_views();
    test_select();
    test_view_moves();

    /* Save writes the binary design by its extension; it reads back the same */
    button("Save");
    CHECK(strstr(g.out, "saved") && !g.dirty);
    CHECK(sqs_read_any(&back, path) == 0 && back.element_count == D.element_count);
    /* QUIT asks the window to close */
    type("quit");
    CHECK(ui.quit == 1);

    button("Iso");
    move(-1, -1);
    CHECK(count(g.area, SQG_RGB(0x40, 0x80, 0xFF)) > 10); /* the Steel level's circle */
    snprintf(path, sizeof path, "%s/station_gui.ppm", dir);
    CHECK(sqg_write_ppm(&cv, path) == 0);

    printf("test_gui: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

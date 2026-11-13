/* SquirrelPaint: the drawing, the tools, undo, the commands, the picture
 * files, and the window driven off screen. Usage: test_paint BUILD_DIR
 * (writes BUILD_DIR/paint.ppm) */
#include "paintgui.h"
#include "paintio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}

static PaintDoc d;
#define WHITE 0xFFFFFFFFu
#define BLACK 0xFF000000u
#define RED 0xFFFF0000u
#define BLUE 0xFF0000FFu

static long count(uint32_t c) {
    long n = 0;
    for (int i = 0; i < d.w * d.h; i++) n += d.px[i] == c;
    return n;
}

static long count_in(int x0, int y0, int x1, int y1, uint32_t c) {
    long n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) n += paint_get(&d, x, y) == c;
    return n;
}

/* 1 when the picture looks the same turned left to right about cx (twice
 * the centre, so halves work) */
static int mirror_x(int cx2) {
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++) {
            int m = cx2 - x;
            if (m >= 0 && m < d.w && paint_get(&d, x, y) != paint_get(&d, m, y)) return 0;
        }
    return 1;
}

static void cmd(const char *line) {
    char msg[120];
    int rc = paint_command(&d, line, msg, sizeof msg);
    check(rc == 0, line, __LINE__);
    if (rc) fprintf(stderr, "  %s: %s\n", line, msg);
}

static int cmd_fails(const char *line, const char *why) {
    char msg[120];
    int rc = paint_command(&d, line, msg, sizeof msg);
    if (rc == 0 || !strstr(msg, why)) {
        fprintf(stderr, "  %s: rc %d, said \"%s\"\n", line, rc, msg);
        return 0;
    }
    return 1;
}

static void test_shapes(void) {
    CHECK(paint_new(&d, 0, 5) == -1 && paint_new(&d, 5, PAINT_MAX_H + 1) == -1);
    CHECK(paint_new(&d, 40, 30) == 0 && d.w == 40 && d.h == 30 && count(WHITE) == 1200);
    CHECK(d.fg == BLACK && d.bg == WHITE && d.size == 4 && d.tool == PAINT_BRUSH && !paint_can_undo(&d));
    CHECK(paint_get(&d, -1, 0) == 0 && paint_get(&d, 40, 0) == 0 && paint_get(&d, 0, 30) == 0);

    /* dabs: one pixel, a 2x2 square, round discs */
    paint_dab(&d, 5, 5, 1, RED);
    CHECK(count(RED) == 1 && paint_get(&d, 5, 5) == RED);
    paint_new(&d, 40, 30);
    paint_dab(&d, 5, 5, 2, RED);
    CHECK(count(RED) == 4 && count_in(5, 5, 6, 6, RED) == 4);
    paint_new(&d, 40, 30);
    paint_dab(&d, 20, 15, 9, RED);
    CHECK(count(RED) >= 57 && count(RED) <= 69); /* about pi 4.5^2 */
    CHECK(count_in(16, 11, 24, 19, RED) == count(RED)); /* within its 9x9 box */
    CHECK(paint_get(&d, 16, 15) == RED && paint_get(&d, 24, 15) == RED && paint_get(&d, 20, 11) == RED);
    CHECK(paint_get(&d, 16, 11) == WHITE); /* not the corners */
    CHECK(mirror_x(40));
    paint_new(&d, 40, 30);
    paint_dab(&d, -2, -2, 9, RED); /* clipped */
    CHECK(count(RED) > 0 && count(RED) < 30);

    /* lines: the ends included, any direction */
    paint_new(&d, 40, 30);
    paint_line(&d, 2, 3, 30, 3, 1, RED);
    CHECK(count(RED) == 29 && paint_get(&d, 2, 3) == RED && paint_get(&d, 30, 3) == RED);
    paint_line(&d, 10, 20, 0, 10, 1, BLUE);
    CHECK(count(BLUE) == 11 && paint_get(&d, 5, 15) == BLUE);
    paint_line(&d, 7, 7, 7, 7, 1, BLUE);
    CHECK(count(BLUE) == 12);
    paint_new(&d, 40, 30);
    paint_line(&d, 5, 15, 35, 15, 5, RED);
    CHECK(count_in(3, 13, 37, 17, RED) == count(RED) && count_in(5, 15, 35, 15, RED) == 31); /* 5 high, round ends */
    CHECK(paint_get(&d, 20, 13) == RED && paint_get(&d, 20, 17) == RED && paint_get(&d, 20, 18) == WHITE);
    paint_new(&d, 40, 30);
    paint_line(&d, -100, -50, 100, 80, 1, RED); /* far off the picture both ends */
    CHECK(count(RED) > 20);

    /* rectangles: outline inside the box, any corner order */
    paint_new(&d, 40, 30);
    paint_rect(&d, 30, 20, 10, 5, 1, 0, RED);
    CHECK(count(RED) == 2 * 21 + 2 * 14 && paint_get(&d, 10, 5) == RED && paint_get(&d, 20, 12) == WHITE);
    paint_new(&d, 40, 30);
    paint_rect(&d, 10, 5, 30, 20, 3, 0, RED);
    CHECK(count(RED) == 21 * 16 - 15 * 10 && paint_get(&d, 12, 12) == RED && paint_get(&d, 13, 12) == WHITE);
    paint_new(&d, 40, 30);
    paint_rect(&d, 10, 5, 30, 20, 3, 1, RED);
    CHECK(count(RED) == 21 * 16);
    paint_new(&d, 40, 30);
    paint_rect(&d, 10, 5, 13, 20, 2, 0, RED); /* thicker than half: all of it */
    CHECK(count(RED) == 4 * 16);
    paint_new(&d, 40, 30);
    paint_rect(&d, -5, -5, 100, 100, 1, 0, RED); /* only the parts on the picture */
    CHECK(count(RED) == 0);

    /* ellipses: in their box, touching its sides, symmetric, closed */
    paint_new(&d, 41, 31);
    paint_ellipse(&d, 5, 5, 35, 25, 1, 1, RED);
    long a = count(RED);
    CHECK(a > 3.14159 * 15.5 * 10.5 * 0.95 && a < 3.14159 * 15.5 * 10.5 * 1.05);
    CHECK(count_in(5, 5, 35, 25, RED) == a);
    CHECK(paint_get(&d, 5, 15) == RED && paint_get(&d, 35, 15) == RED && paint_get(&d, 20, 5) == RED &&
          paint_get(&d, 20, 25) == RED);
    CHECK(paint_get(&d, 5, 5) == WHITE && paint_get(&d, 35, 25) == WHITE);
    CHECK(mirror_x(40));
    paint_new(&d, 41, 31);
    paint_ellipse(&d, 35, 25, 5, 5, 1, 0, RED);
    long o = count(RED);
    CHECK(o > 60 && o < 120 && paint_get(&d, 20, 15) == WHITE && mirror_x(40));
    /* the outline holds a fill in: nothing leaks out to the corners */
    CHECK(paint_flood(&d, 20, 15, BLUE, 0) > 300 && paint_get(&d, 0, 0) == WHITE && paint_get(&d, 6, 6) == WHITE);
    paint_new(&d, 41, 31);
    paint_ellipse(&d, 5, 5, 35, 25, 4, 0, RED);
    CHECK(count(RED) > o * 3 && paint_get(&d, 20, 15) == WHITE);
    paint_new(&d, 41, 31);
    paint_ellipse(&d, 10, 10, 10, 10, 1, 0, RED); /* a box of one pixel */
    CHECK(count(RED) == 1);
    paint_new(&d, 41, 31);
    paint_ellipse(&d, 10, 10, 20, 11, 1, 0, RED); /* two rows */
    CHECK(count(RED) == count_in(10, 10, 20, 11, RED) && count(RED) >= 18);

    /* flood fill: four-way, within tolerance */
    paint_new(&d, 20, 20);
    paint_rect(&d, 5, 5, 14, 14, 1, 0, BLACK);
    CHECK(paint_flood(&d, 9, 9, RED, 0) == 64 && count(RED) == 64);
    CHECK(paint_flood(&d, 9, 9, RED, 0) == 0); /* already that colour */
    CHECK(paint_flood(&d, 0, 0, BLUE, 0) == 400 - 100);
    CHECK(paint_flood(&d, -1, 0, BLUE, 0) == 0 && paint_flood(&d, 0, 20, BLUE, 0) == 0);
    paint_new(&d, 20, 1);
    for (int x = 0; x < 20; x++) d.px[x] = 0xFF000000u | (uint32_t)(x * 10) << 16; /* a ramp of red */
    CHECK(paint_flood(&d, 0, 0, BLUE, 25) == 3);   /* 0, 10, 20 */
    CHECK(paint_flood(&d, 10, 0, BLUE, 255) == 20); /* all: blue is within 255 of every channel */
    paint_new(&d, 3, 3);
    d.px[1] = d.px[3] = d.px[5] = d.px[7] = BLACK; /* corners only touch diagonally */
    CHECK(paint_flood(&d, 0, 0, RED, 0) == 1);
}

static void stroke(int tool, int x0, int y0, int x1, int y1, int button, int square) {
    d.tool = tool;
    paint_press(&d, x0, y0, button, square);
    paint_drag(&d, (x0 + x1) / 2 + 7, (y0 + y1) / 2 - 3, square);
    paint_release(&d, x1, y1, square);
}

static void test_tools(void) {
    paint_new(&d, 60, 40);
    d.fg = RED;
    d.bg = BLUE;
    d.size = 1;
    /* a shape is drawn afresh as it is dragged: only the last one stays */
    d.tool = PAINT_RECT;
    paint_press(&d, 10, 10, 1, 0);
    paint_drag(&d, 50, 35, 0);
    CHECK(paint_get(&d, 50, 35) == RED);
    paint_drag(&d, 20, 20, 0);
    CHECK(paint_get(&d, 50, 35) == WHITE && paint_get(&d, 20, 20) == RED);
    paint_release(&d, 30, 25, 0);
    CHECK(count(RED) == 2 * 21 + 2 * 14 && paint_can_undo(&d) && d.changed);
    /* Shift: a square, a circle, a line at 45 degrees */
    paint_new(&d, 60, 40);
    stroke(PAINT_RECT, 5, 5, 25, 10, 1, 1);
    CHECK(paint_get(&d, 25, 25) == BLACK && paint_get(&d, 25, 26) == WHITE);
    paint_new(&d, 60, 40);
    stroke(PAINT_LINE, 5, 5, 30, 7, 1, 1); /* nearly flat: flat */
    CHECK(count_in(5, 5, 30, 5, BLACK) == 26 && count(BLACK) == count_in(0, 3, 59, 7, BLACK));
    paint_new(&d, 60, 40);
    d.size = 1;
    stroke(PAINT_LINE, 5, 5, 20, 17, 1, 1); /* steep-ish: the diagonal */
    CHECK(paint_get(&d, 20, 20) == BLACK && paint_get(&d, 12, 12) == BLACK && count(BLACK) == 16);
    paint_new(&d, 60, 40);
    d.size = 1;
    stroke(PAINT_ELLIPSE, 30, 20, 10, 15, 1, 1); /* up and left: a circle of the larger side */
    CHECK(paint_get(&d, 10, 0) == WHITE && count_in(10, 0, 30, 20, BLACK) == count(BLACK)); /* the box 10..30, 0..20 */
    CHECK(paint_get(&d, 10, 10) == BLACK && paint_get(&d, 20, 0) == BLACK && paint_get(&d, 20, 10) == WHITE);
    /* the buttons: the primary paints fg, the secondary bg; the eraser the other way */
    paint_new(&d, 60, 40);
    d.fg = RED;
    d.bg = BLUE;
    stroke(PAINT_PENCIL, 5, 5, 50, 5, 2, 0);
    CHECK(count(BLUE) > 40 && !count(RED));
    d.size = 6;
    d.tool = PAINT_BRUSH;
    paint_press(&d, 5, 20, 1, 0);
    paint_drag(&d, 30, 20, 0);
    paint_release(&d, 50, 20, 0);
    CHECK(paint_get(&d, 30, 22) == RED && paint_get(&d, 30, 18) == RED && paint_get(&d, 30, 24) == WHITE);
    long red = count(RED);
    d.tool = PAINT_ERASER;
    paint_press(&d, 5, 20, 1, 0);
    paint_release(&d, 50, 20, 0);
    CHECK(count(RED) < red / 4 && paint_get(&d, 30, 20) == BLUE); /* rubbed out to bg */
    paint_press(&d, 5, 30, 2, 0);
    paint_release(&d, 50, 30, 0);
    CHECK(paint_get(&d, 30, 30) == RED);
    /* the picker sets a colour and is no step */
    int steps = d.nsteps;
    d.tool = PAINT_PICKER;
    paint_press(&d, 30, 30, 2, 0);
    CHECK(d.bg == RED && !d.active);
    paint_press(&d, 0, 39, 1, 0);
    paint_release(&d, 0, 39, 0);
    CHECK(d.fg == WHITE && d.nsteps == steps);
    paint_press(&d, 99, 99, 1, 0);
    CHECK(d.fg == WHITE);
    /* fill as a tool, one step */
    d.fg = BLACK;
    d.tool = PAINT_FILL;
    d.tolerance = 0;
    paint_press(&d, 0, 39, 1, 0);
    paint_release(&d, 0, 39, 0);
    CHECK(paint_get(&d, 0, 0) == BLACK && d.nsteps == steps + 1 && !d.active);
    paint_press(&d, 0, 39, 1, 0); /* nothing to change: no step */
    CHECK(d.nsteps == steps + 1);
    /* Escape: the stroke taken back, no step */
    d.tool = PAINT_BRUSH;
    paint_press(&d, 10, 10, 1, 0);
    paint_drag(&d, 40, 10, 0);
    CHECK(paint_get(&d, 25, 10) == BLACK || d.fg == BLACK);
    paint_cancel(&d);
    CHECK(!d.active && d.nsteps == steps + 1 && paint_get(&d, 25, 12) == paint_get(&d, 25, 25));
    /* a press while one is held ends the first */
    paint_press(&d, 10, 10, 1, 0);
    paint_press(&d, 20, 10, 1, 0);
    paint_release(&d, 20, 10, 0);
    CHECK(d.nsteps == steps + 3);
    /* off the picture: clipped, and no step when nothing changed */
    stroke(PAINT_LINE, -50, -50, -10, -10, 1, 0);
    CHECK(d.nsteps == steps + 3);
}

static void test_undo(void) {
    paint_new(&d, 50, 50);
    d.size = 1;
    CHECK(paint_undo(&d) == -1 && paint_redo(&d) == -1);
    cmd("rect 0 0 9 9 square");
    cmd("color ff0000");
    cmd("line 0 20 49 20");
    cmd("fill on");
    cmd("ellipse 20 30 40 45");
    long a = count(BLACK), b = count(RED);
    CHECK(a == 36 && b > 51 + 200);
    CHECK(paint_undo(&d) == 0 && count(RED) == 50 && count(BLACK) == 36);
    CHECK(paint_undo(&d) == 0 && count(RED) == 0);
    CHECK(paint_undo(&d) == 0 && count(WHITE) == 2500 && paint_undo(&d) == -1);
    CHECK(paint_redo(&d) == 0 && paint_redo(&d) == 0 && count(RED) == 50);
    /* new drawing after an undo: the undone step is gone for good */
    cmd("undo");
    cmd("pencil 5 40 6 40");
    CHECK(!paint_can_redo(&d) && paint_redo(&d) == -1);
    cmd("undo");
    cmd("undo");
    CHECK(count(WHITE) == 2500);
    CHECK(cmd_fails("undo", "nothing to undo") && cmd_fails("redo extra", "takes nothing"));
    cmd("redo");
    cmd("redo");
    CHECK(count(BLACK) == 36 && count(RED) == 2 && !paint_can_redo(&d));
    /* only the last PAINT_UNDO_STEPS strokes are kept */
    paint_new(&d, 50, 50);
    for (int i = 0; i < PAINT_UNDO_STEPS + 5; i++) {
        char t[40];
        snprintf(t, sizeof t, "pencil %d %d", i % 50, i / 50);
        cmd(t);
    }
    int k = 0;
    while (paint_undo(&d) == 0) k++;
    CHECK(k == PAINT_UNDO_STEPS && count(WHITE) == 2500 - 5);
    while (paint_redo(&d) == 0) k--;
    CHECK(k == 0 && count(WHITE) == 2500 - PAINT_UNDO_STEPS - 5);
    /* and only what fits in the store: whole-picture steps on the biggest picture */
    CHECK(paint_new(&d, PAINT_MAX_W, PAINT_MAX_H) == 0);
    for (int i = 0; i < 4; i++) {
        cmd(i % 2 ? "bg 00ff00" : "bg 0000ff");
        cmd("clear");
    }
    k = 0;
    while (paint_undo(&d) == 0) k++;
    CHECK(k == (int)(PAINT_UNDO_PX / ((size_t)PAINT_MAX_W * PAINT_MAX_H)) && d.px[0] == 0xFF00FF00u); /* the second clear */
    paint_load(&d, d.px, 10, 10);
    CHECK(!paint_can_undo(&d) && !paint_can_redo(&d) && d.w == 10);
}

static void test_commands(void) {
    paint_new(&d, 30, 30);
    cmd("");
    cmd("NEW 64 48");
    CHECK(d.w == 64 && d.h == 48);
    cmd("Color #12aBcD");
    CHECK(d.fg == 0xFF12ABCDu);
    cmd("bg 000000");
    cmd("size 9");
    cmd("tolerance 0");
    CHECK(d.bg == BLACK && d.size == 9 && d.tolerance == 0);
    cmd("brush 10,10, 20,10  30,20");
    CHECK(paint_get(&d, 20, 10) == 0xFF12ABCDu && d.tool == PAINT_BRUSH && paint_can_undo(&d));
    cmd("eraser 20 10");
    CHECK(paint_get(&d, 20, 10) == BLACK);
    cmd("brush right 50 40");
    CHECK(paint_get(&d, 50, 40) == BLACK);
    cmd("pick 10 10");
    CHECK(d.fg == 0xFF12ABCDu);
    cmd("pick right 0 0");
    CHECK(d.bg == WHITE);
    cmd("clear");
    CHECK(count(WHITE) == 64 * 48);
    cmd("fill 0 0");
    CHECK(count(0xFF12ABCDu) == 64 * 48);
    cmd("fill off");
    CHECK(!d.filled);
    struct {
        const char *line, *why;
    } bad[] = {
        {"paint 1 2", "unknown"},
        {"line 1 2 3", "X0 Y0 X1 Y1"},
        {"rect 1 2 3 4 5", "X0 Y0 X1 Y1"},
        {"brush 1", "X Y [X Y"},
        {"brush 1 2 3", "X Y [X Y"},
        {"fill 1", "X Y"},
        {"fill on 3 4", "fill on or fill off"},
        {"pick 1 2 3 4", "X Y"},
        {"line 1 2 x 4", "not a number: x"},
        {"line 1 2 9999999 4", "not a number"},
        {"color red", "RRGGBB"},
        {"color 12345", "RRGGBB"},
        {"color 123456 7", "RRGGBB"},
        {"size 0", "1 to 64"},
        {"size 65", "1 to 64"},
        {"tolerance 256", "0 to 255"},
        {"new 0 10", "new W H"},
        {"new 10", "new W H"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) check(cmd_fails(bad[i].line, bad[i].why), bad[i].line, __LINE__);
    CHECK(d.w == 64 && d.size == 9); /* a bad line changes nothing */
    uint32_t c;
    CHECK(paint_parse_color("#FFffFF", 7, &c) == 0 && c == WHITE);
    CHECK(paint_parse_color("ggffff", 6, &c) == -1 && paint_parse_color("#fff", 4, &c) == -1);
}


/* a picture whose every pixel says where it is */
static void numbered(int w, int h) {
    paint_new(&d, w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) d.px[y * w + x] = 0xFF000000u | (uint32_t)(y << 8 | x);
}
static uint32_t at_xy(int x, int y) { return 0xFF000000u | (uint32_t)(y << 8 | x); }

static void test_selection(void) {
    paint_new(&d, 20, 10);
    paint_rect(&d, 2, 2, 3, 3, 1, 1, RED);
    CHECK(paint_select(&d, 3, 3, 2, 2) == 0 && d.sel.x == 2 && d.sel.w == 2 && !d.floating);
    CHECK(paint_move_selection(&d, 5, 0) == 0 && d.floating);
    CHECK(paint_get(&d, 2, 2) == WHITE && paint_get(&d, 7, 2) == WHITE && paint_shown(&d, 7, 2) == RED);
    CHECK(paint_shown(&d, 2, 2) == WHITE && paint_shown(&d, 0, 0) == WHITE);
    paint_move_selection(&d, 1, 1); /* still one step */
    paint_drop(&d);
    CHECK(!d.floating && paint_get(&d, 8, 3) == RED && count(RED) == 4 && d.sel.x == 8 && d.sel.w == 2);
    CHECK(paint_undo(&d) == 0 && paint_get(&d, 2, 2) == RED && count(RED) == 4 && paint_get(&d, 8, 3) == WHITE);
    CHECK(!d.sel.w && paint_redo(&d) == 0 && paint_get(&d, 8, 3) == RED && paint_get(&d, 2, 2) == WHITE);
    /* moved partly off the picture: the part on it stays */
    paint_select(&d, 8, 3, 9, 4);
    paint_move_selection(&d, 11, 0);
    paint_drop(&d);
    CHECK(count(RED) == 2 && paint_get(&d, 19, 3) == RED && d.sel.x == 19 && d.sel.w == 1);
    paint_undo(&d);
    CHECK(count(RED) == 4 && paint_get(&d, 9, 4) == RED);
    /* drawing puts down what floats first */
    paint_select(&d, 8, 3, 9, 4);
    paint_move_selection(&d, 0, 4);
    d.tool = PAINT_PENCIL;
    paint_press(&d, 0, 0, 1, 0);
    paint_release(&d, 0, 0, 0);
    CHECK(!d.floating && paint_get(&d, 8, 7) == RED && paint_get(&d, 0, 0) == BLACK);
    CHECK(paint_undo(&d) == 0 && paint_get(&d, 0, 0) == WHITE && paint_undo(&d) == 0 && paint_get(&d, 8, 3) == RED);

    /* the clipboard */
    d.clip_w = d.clip_h = 0;
    paint_select_none(&d);
    CHECK(paint_copy(&d) == -1 && paint_cut(&d) == -1 && paint_delete(&d) == -1 && paint_paste(&d, 0, 0) == -1);
    CHECK(paint_move_selection(&d, 1, 1) == -1 && paint_lift(&d) == -1);
    paint_select(&d, 8, 3, 9, 4);
    CHECK(paint_copy(&d) == 0 && d.clip_w == 2 && d.clip_h == 2 && count(RED) == 4);
    CHECK(paint_paste(&d, 15, 6) == 0 && d.floating && d.sel.x == 15 && count(RED) == 4);
    paint_drop(&d);
    CHECK(count(RED) == 8 && paint_get(&d, 16, 7) == RED);
    CHECK(paint_undo(&d) == 0 && count(RED) == 4); /* the paste alone */
    CHECK(paint_paste(&d, -5, 99) == 0 && d.sel.x == 0 && d.sel.y == 9); /* kept on the picture */
    paint_drop(&d);
    CHECK(count(RED) == 6); /* the one row of it on the picture */
    paint_undo(&d);
    paint_select(&d, 8, 3, 9, 4);
    CHECK(paint_cut(&d) == 0 && count(RED) == 0 && !d.sel.w && d.clip_w == 2);
    CHECK(paint_paste(&d, 0, 0) == 0);
    paint_drop(&d);
    CHECK(count(RED) == 4 && paint_get(&d, 1, 1) == RED);
    paint_undo(&d);
    paint_undo(&d);
    CHECK(count(RED) == 4 && paint_get(&d, 8, 3) == RED);
    /* delete: in place, or what floats (its hole stays) */
    paint_select(&d, 8, 3, 8, 3);
    CHECK(paint_delete(&d) == 0 && count(RED) == 3);
    paint_undo(&d);
    paint_select(&d, 8, 3, 9, 4);
    paint_move_selection(&d, 2, 0);
    CHECK(paint_delete(&d) == 0 && !d.floating && count(RED) == 0);
    CHECK(paint_undo(&d) == 0 && count(RED) == 4 && paint_get(&d, 8, 3) == RED);
    /* copy from what floats */
    paint_select(&d, 8, 3, 9, 4);
    paint_move_selection(&d, 0, 1);
    CHECK(paint_copy(&d) == 0 && d.clip[0] == RED && d.clip[3] == RED);
    paint_select_none(&d);
    CHECK(!d.floating && !d.sel.w && paint_get(&d, 8, 5) == RED);
    CHECK(paint_select(&d, 30, 30, 40, 40) == -1 && !d.sel.w);

    /* the select tool: a rectangle by dragging; dragged inside, it moves; a click clears it */
    paint_new(&d, 20, 10);
    paint_dab(&d, 5, 5, 1, RED);
    d.tool = PAINT_SELECT;
    paint_press(&d, 7, 7, 1, 0);
    paint_drag(&d, 3, 3, 0);
    paint_release(&d, 4, 4, 0);
    CHECK(d.sel.x == 4 && d.sel.y == 4 && d.sel.w == 4 && d.sel.h == 4 && !d.sel_mode && !d.floating);
    paint_press(&d, 5, 5, 1, 0);
    paint_drag(&d, 10, 6, 0);
    CHECK(d.floating && paint_shown(&d, 10, 6) == RED && paint_get(&d, 5, 5) == WHITE);
    paint_release(&d, 12, 5, 0);
    CHECK(d.floating && paint_shown(&d, 12, 5) == RED && d.sel.x == 11);
    paint_press(&d, 0, 0, 1, 0);
    paint_release(&d, 0, 0, 0);
    CHECK(!d.floating && !d.sel.w && paint_get(&d, 12, 5) == RED);
    CHECK(paint_undo(&d) == 0 && paint_get(&d, 5, 5) == RED && count(RED) == 1);
    paint_press(&d, 2, 2, 1, 0);
    paint_drag(&d, 9, 8, 0);
    paint_cancel(&d); /* Escape while drawing it */
    CHECK(!d.sel.w && !d.sel_mode);
}

static void test_transforms(void) {
    numbered(3, 2);
    CHECK(paint_flip(&d, 0) == 0 && paint_get(&d, 0, 0) == at_xy(2, 0) && paint_get(&d, 2, 1) == at_xy(0, 1));
    CHECK(paint_flip(&d, 1) == 0 && paint_get(&d, 0, 0) == at_xy(2, 1));
    paint_undo(&d);
    paint_undo(&d);
    CHECK(paint_get(&d, 0, 0) == at_xy(0, 0));
    /* a quarter turn clockwise: 3 x 2 becomes 2 x 3, the left column on top */
    CHECK(paint_rotate(&d, 1) == 0 && d.w == 2 && d.h == 3);
    CHECK(paint_get(&d, 0, 0) == at_xy(0, 1) && paint_get(&d, 1, 0) == at_xy(0, 0) && paint_get(&d, 1, 2) == at_xy(2, 0));
    CHECK(paint_rotate(&d, -1) == 0 && d.w == 3 && paint_get(&d, 2, 1) == at_xy(2, 1));
    CHECK(paint_rotate(&d, 2) == 0 && paint_get(&d, 0, 0) == at_xy(2, 1));
    CHECK(paint_rotate(&d, 4) == 0 && paint_rotate(&d, 2) == 0 && paint_get(&d, 0, 0) == at_xy(0, 0));
    /* undo and redo over size changes */
    CHECK(paint_undo(&d) == 0 && paint_undo(&d) == 0 && paint_undo(&d) == 0 && d.w == 2 && d.h == 3);
    CHECK(paint_get(&d, 1, 0) == at_xy(0, 0));
    CHECK(paint_undo(&d) == 0 && d.w == 3 && d.h == 2 && paint_get(&d, 2, 1) == at_xy(2, 1));
    CHECK(paint_redo(&d) == 0 && d.w == 2 && paint_redo(&d) == 0 && d.w == 3 && paint_get(&d, 2, 1) == at_xy(2, 1));
    /* resize: by the nearest pixel, or smooth */
    numbered(2, 2);
    CHECK(paint_resize(&d, 4, 6, 0) == 0 && d.w == 4 && d.h == 6);
    CHECK(paint_get(&d, 1, 2) == at_xy(0, 0) && paint_get(&d, 2, 3) == at_xy(1, 1) && paint_get(&d, 3, 5) == at_xy(1, 1));
    CHECK(paint_undo(&d) == 0 && d.w == 2 && d.h == 2);
    CHECK(paint_resize(&d, 0, 5, 0) == -1 && paint_resize(&d, PAINT_MAX_W + 1, 5, 0) == -1 && d.w == 2);
    paint_new(&d, 2, 1);
    d.px[0] = BLACK;
    d.px[1] = WHITE;
    CHECK(paint_resize(&d, 4, 1, 1) == 0);
    CHECK(d.px[0] == BLACK && d.px[3] == WHITE && (d.px[1] & 0xFF) > 0x30 && (d.px[1] & 0xFF) < 0x50 &&
          (d.px[2] & 0xFF) > 0xB0 && (d.px[2] & 0xFF) < 0xD0); /* a quarter and three quarters */
    paint_new(&d, 4, 4);
    for (int i = 0; i < 16; i++) d.px[i] = (i / 4 + i % 4) % 2 ? WHITE : BLACK; /* a checkerboard */
    CHECK(paint_resize(&d, 1, 1, 1) == 0 && (d.px[0] & 0xFF) > 0x60 && (d.px[0] & 0xFF) < 0xA0); /* grey */
    /* crop to the selection; canvas grows with the background colour or cuts */
    numbered(10, 8);
    CHECK(paint_crop(&d) == -1);
    paint_select(&d, 2, 3, 5, 4);
    CHECK(paint_crop(&d) == 0 && d.w == 4 && d.h == 2 && paint_get(&d, 0, 0) == at_xy(2, 3) && !d.sel.w);
    CHECK(paint_undo(&d) == 0 && d.w == 10 && d.h == 8 && paint_get(&d, 9, 7) == at_xy(9, 7));
    d.bg = RED;
    CHECK(paint_canvas(&d, 12, 5) == 0 && d.w == 12 && d.h == 5 && paint_get(&d, 11, 0) == RED && paint_get(&d, 9, 4) == at_xy(9, 4));
    CHECK(paint_canvas(&d, 0, 5) == -1 && paint_canvas(&d, 12, 5) == 0 && d.done == 1); /* the same size: no step */
    CHECK(paint_undo(&d) == 0 && d.w == 10 && d.h == 8);
    CHECK(paint_invert(&d) == 0 && paint_get(&d, 0, 0) == WHITE && paint_get(&d, 1, 0) == 0xFFFFFFFEu);
    paint_undo(&d);
    /* on a selection: only it changes, lifted, and one step with its drop */
    numbered(10, 8);
    paint_select(&d, 0, 0, 2, 0);
    CHECK(paint_flip(&d, 0) == 0 && d.floating && paint_shown(&d, 0, 0) == at_xy(2, 0));
    CHECK(paint_rotate(&d, 1) == 0 && d.sel.w == 1 && d.sel.h == 3 && paint_shown(&d, 0, 2) == at_xy(0, 0));
    CHECK(paint_resize(&d, 2, 6, 0) == 0 && d.sel.w == 2 && d.sel.h == 6 && d.w == 10);
    CHECK(paint_invert(&d) == 0);
    paint_drop(&d);
    CHECK(paint_get(&d, 1, 5) == (at_xy(0, 0) ^ 0x00FFFFFFu) && paint_get(&d, 2, 0) == d.bg);
    CHECK(paint_undo(&d) == 0 && paint_get(&d, 2, 0) == at_xy(2, 0) && paint_get(&d, 1, 5) == at_xy(1, 5));
    /* big ones fit the undo store as whole steps */
    CHECK(paint_new(&d, PAINT_MAX_W, PAINT_MAX_H / 2) == 0);
    d.px[0] = RED;
    CHECK(paint_rotate(&d, 1) == 0 && d.w == PAINT_MAX_H / 2 && paint_get(&d, PAINT_MAX_H / 2 - 1, 0) == RED);
    CHECK(paint_undo(&d) == 0 && d.w == PAINT_MAX_W && paint_get(&d, 0, 0) == RED);
}

static void test_selection_commands(void) {
    numbered(16, 12);
    cmd("select 2 2 5 5");
    CHECK(d.sel.w == 4);
    cmd("copy");
    cmd("move 4 0");
    cmd("drop");
    CHECK(paint_get(&d, 6, 2) == at_xy(2, 2) && !d.floating);
    cmd("paste 10 8");
    cmd("flip h");
    cmd("flip v");
    cmd("drop");
    CHECK(paint_get(&d, 13, 11) == at_xy(2, 2));
    cmd("select all");
    CHECK(d.sel.w == 16 && d.sel.h == 12);
    cmd("select none");
    cmd("rotate 270");
    CHECK(d.w == 12 && d.h == 16);
    cmd("rotate 90");
    cmd("resize 32 24");
    CHECK(d.w == 32 && d.h == 24);
    cmd("resize 16 12 smooth");
    cmd("select 0 0 3 3");
    cmd("crop");
    CHECK(d.w == 4 && d.h == 4);
    cmd("canvas 6 6");
    cmd("invert");
    cmd("select 0 0 1 1");
    cmd("cut");
    cmd("select 2 2 3 3");
    cmd("delete");
    cmd("paste");
    CHECK(d.floating && d.sel.x == 0);
    struct {
        const char *line, *why;
    } bad[] = {
        {"select 1 2 3", "select X0 Y0 X1 Y1"},
        {"select everything", "select X0 Y0 X1 Y1"},
        {"select 100 100 200 200", "off the picture"},
        {"copy 1", "copy takes nothing"},
        {"crop now", "crop takes nothing"},
        {"paste 1", "paste, or paste X Y"},
        {"move 1", "move DX DY"},
        {"flip", "flip h or flip v"},
        {"flip h v", "flip h or flip v"},
        {"rotate 45", "rotate 90, 180, 270"},
        {"rotate", "rotate 90, 180, 270"},
        {"resize 10", "resize W H"},
        {"resize 10 10 sharp", "resize W H"},
        {"resize 0 10", "out of range"},
        {"canvas 10 10 smooth", "canvas W H"},
        {"canvas 2000 10", "out of range"},
        {"invert all", "invert takes nothing"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) check(cmd_fails(bad[i].line, bad[i].why), bad[i].line, __LINE__);
    cmd("select none");
    CHECK(cmd_fails("copy", "nothing is selected") && cmd_fails("move 1 1", "nothing is selected") &&
          cmd_fails("crop", "nothing is selected"));
    d.clip_w = 0;
    CHECK(cmd_fails("paste", "nothing to paste"));
}

/* ---- files ---- */

static uint8_t buf[1 << 22];
static uint32_t pic[PAINT_MAX_W * PAINT_MAX_H];

static void le16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void le32(uint8_t *p, unsigned v) {
    le16(p, v & 0xFFFF);
    le16(p + 2, v >> 16);
}

/* a BMP head: file header and a Windows header of hs bytes (or OS/2's 12) */
static size_t bmp_head(int hs, int w, int h, int bpp, int comp, int npal, int palsize, size_t data_at) {
    memset(buf, 0, 4096);
    buf[0] = 'B';
    buf[1] = 'M';
    le32(buf + 10, (unsigned)data_at);
    le32(buf + 14, (unsigned)hs);
    if (hs == 12) {
        le16(buf + 18, (unsigned)w);
        le16(buf + 20, (unsigned)h);
        le16(buf + 22, 1);
        le16(buf + 24, (unsigned)bpp);
    } else {
        le32(buf + 18, (unsigned)w);
        le32(buf + 22, (unsigned)h);
        le16(buf + 26, 1);
        le16(buf + 28, (unsigned)bpp);
        le32(buf + 30, (unsigned)comp);
        le32(buf + 46, (unsigned)npal);
    }
    (void)palsize;
    return 14 + (size_t)hs;
}

static int read(size_t n, int *w, int *h, char *msg) {
    return paint_read_image(buf, n, pic, sizeof pic / sizeof pic[0], w, h, msg, 120);
}

static void test_files(void) {
    char msg[120];
    int w, h;
    /* round trips, an odd width for the BMP's row padding */
    paint_new(&d, 13, 7);
    for (int i = 0; i < 13 * 7; i++) d.px[i] = 0xFF000000u | (uint32_t)(i * 2654435761u >> 8);
    size_t n = paint_write_image(PAINT_FILE_BMP, d.px, 13, 7, buf, sizeof buf);
    CHECK(n == 54 + 40 * 7 && buf[0] == 'B' && buf[28] == 24);
    CHECK(paint_write_image(PAINT_FILE_BMP, d.px, 13, 7, buf, 100) == n); /* says what it needs */
    n = paint_write_image(PAINT_FILE_BMP, d.px, 13, 7, buf, sizeof buf);
    CHECK(read(n, &w, &h, msg) == 0 && w == 13 && h == 7 && !memcmp(pic, d.px, sizeof(uint32_t) * 91));
    n = paint_write_image(PAINT_FILE_PPM, d.px, 13, 7, buf, sizeof buf);
    CHECK(n == 12 + 3 * 91 && !memcmp(buf, "P6\n13 7\n255\n", 12));
    CHECK(read(n, &w, &h, msg) == 0 && w == 13 && h == 7 && !memcmp(pic, d.px, sizeof(uint32_t) * 91));
    CHECK(read(n - 1, &w, &h, msg) == -1 && strstr(msg, "cut short"));
    CHECK(paint_write_image(PAINT_FILE_PPM, d.px, 0, 7, buf, sizeof buf) == 0);
    CHECK(read(n, &w, &h, msg) == 0);
    CHECK(paint_read_image(buf, n, pic, 90, &w, &h, msg, sizeof msg) == -1 && strstr(msg, "bigger"));

    /* 8 bits with a palette, top-down */
    size_t at = bmp_head(40, 3, 2, 8, 0, 2, 4, 0);
    le32(buf + at, 0x00FF0000);     /* index 0: red (B G R 0 in the file: 00 00 FF 00) */
    le32(buf + at + 4, 0x000000FF); /* index 1: blue */
    le32(buf + 22, (unsigned)-2);   /* top-down */
    le32(buf + 10, (unsigned)(at + 8));
    uint8_t *px = buf + at + 8;
    px[0] = 0, px[1] = 1, px[2] = 0, px[4] = 1, px[5] = 1, px[6] = 0;
    CHECK(read(at + 8 + 8, &w, &h, msg) == 0 && w == 3 && h == 2);
    CHECK(pic[0] == RED && pic[1] == BLUE && pic[2] == RED && pic[3] == BLUE && pic[4] == BLUE && pic[5] == RED);
    CHECK(read(at + 8 + 7, &w, &h, msg) == -1 && strstr(msg, "cut short"));
    /* 1 bit, bottom-up: the last row first */
    at = bmp_head(40, 10, 2, 1, 0, 0, 4, 0);
    le32(buf + at, 0x000000);
    le32(buf + at + 4, 0xFFFFFF);
    le32(buf + 10, (unsigned)(at + 8));
    px = buf + at + 8;
    px[0] = 0x80, px[1] = 0x40; /* the bottom row: x 0 and x 9 white */
    px[4] = 0x00, px[5] = 0x00;
    CHECK(read(at + 16, &w, &h, msg) == 0 && w == 10 && h == 2);
    CHECK(pic[10] == WHITE && pic[19] == WHITE && pic[11] == BLACK && pic[0] == BLACK);
    /* 4 bits */
    at = bmp_head(40, 3, 1, 4, 0, 16, 4, 0);
    for (int i = 0; i < 16; i++) le32(buf + at + 4 * i, (unsigned)(i * 0x111111));
    le32(buf + 10, (unsigned)(at + 64));
    buf[at + 64] = 0x2F;
    buf[at + 65] = 0x10;
    CHECK(read(at + 68, &w, &h, msg) == 0 && pic[0] == 0xFF222222u && pic[1] == 0xFFFFFFFFu && pic[2] == 0xFF111111u);
    /* 16 bits: 555 plain, 565 by masks */
    at = bmp_head(40, 2, 1, 16, 0, 0, 0, 54);
    le16(buf + 54, 0x7C00);
    le16(buf + 56, 0x001F);
    CHECK(read(58, &w, &h, msg) == 0 && pic[0] == RED && pic[1] == BLUE);
    at = bmp_head(40, 2, 1, 16, 3, 0, 0, 66);
    le32(buf + 54, 0xF800);
    le32(buf + 58, 0x07E0);
    le32(buf + 62, 0x001F);
    le16(buf + 66, 0x07E0);
    le16(buf + 68, 0x8410); /* half of each: 16/31 and 32/63 */
    CHECK(read(70, &w, &h, msg) == 0 && pic[0] == 0xFF00FF00u && pic[1] == 0xFF848284u);
    /* 32 bits with masks in a V4 header, the alpha left out */
    at = bmp_head(108, 1, 1, 32, 3, 0, 0, 122);
    le32(buf + 54, 0x0000FF00);
    le32(buf + 58, 0x00FF0000);
    le32(buf + 62, 0xFF000000);
    le32(buf + 122, 0x123456FF);
    CHECK(read(126, &w, &h, msg) == 0 && pic[0] == 0xFF563412u);
    /* 32 plain, 24 plain */
    at = bmp_head(40, 1, 1, 32, 0, 0, 0, 54);
    le32(buf + 54, 0x00ABCDEF);
    CHECK(read(58, &w, &h, msg) == 0 && pic[0] == 0xFFABCDEFu);
    /* OS/2 1.x header: 8 bits, three-byte palette entries */
    at = bmp_head(12, 2, 1, 8, 0, 0, 3, 0);
    buf[at] = 0x10, buf[at + 1] = 0x20, buf[at + 2] = 0x30;
    buf[at + 3] = 0x40, buf[at + 4] = 0x50, buf[at + 5] = 0x60;
    for (int i = 6; i < 3 * 256; i++) buf[at + (size_t)i] = 0;
    le32(buf + 10, (unsigned)(at + 3 * 256));
    buf[at + 768] = 1;
    buf[at + 769] = 0;
    CHECK(read(at + 768 + 4, &w, &h, msg) == 0 && w == 2 && pic[0] == 0xFF605040u && pic[1] == 0xFF302010u);
    /* what it does not read */
    bmp_head(40, 2, 2, 8, 1, 0, 0, 54);
    CHECK(read(200, &w, &h, msg) == -1 && strstr(msg, "compressed"));
    bmp_head(40, 2, 2, 12, 0, 0, 0, 54);
    CHECK(read(200, &w, &h, msg) == -1 && strstr(msg, "depth"));
    bmp_head(40, 2, 2, 24, 3, 0, 0, 54);
    CHECK(read(200, &w, &h, msg) == -1 && strstr(msg, "masks"));
    bmp_head(40, 0, 2, 24, 0, 0, 0, 54);
    CHECK(read(200, &w, &h, msg) == -1 && strstr(msg, "no size"));
    bmp_head(20, 2, 2, 24, 0, 0, 0, 54);
    CHECK(read(200, &w, &h, msg) == -1 && strstr(msg, "header"));
    bmp_head(40, 2, 2, 24, 0, 0, 0, 5000);
    CHECK(read(200, &w, &h, msg) == -1 && strstr(msg, "cut short"));
    bmp_head(40, 2, 2, 24, 0, 0, 0, 54);
    CHECK(read(20, &w, &h, msg) == -1 && strstr(msg, "cut short"));
    bmp_head(40, 2000, 2000, 24, 0, 0, 0, 54);
    CHECK(read(200, &w, &h, msg) == -1 && strstr(msg, "bigger"));
    memcpy(buf, "\x89PNG\r\n\x1a\n", 8);
    CHECK(read(8, &w, &h, msg) == -1 && strstr(msg, "PNG"));
    memcpy(buf, "\xFF\xD8\xFF", 3);
    CHECK(read(3, &w, &h, msg) == -1 && strstr(msg, "JPEG"));
    CHECK(read(0, &w, &h, msg) == -1 && strstr(msg, "not a picture"));

    /* netpbm in text, grey, sixteen bits, with comments */
    const char *p3 = "P3\n# a comment\n2 1 # more\n15\n15 0 0   0 0 15\n";
    memcpy(buf, p3, strlen(p3));
    CHECK(read(strlen(p3), &w, &h, msg) == 0 && w == 2 && h == 1 && pic[0] == RED && pic[1] == BLUE);
    CHECK(read(strlen(p3) - 4, &w, &h, msg) == -1 && strstr(msg, "cut short"));
    const char *p2 = "P2 2 1 255 0 255\n";
    memcpy(buf, p2, strlen(p2));
    CHECK(read(strlen(p2), &w, &h, msg) == 0 && pic[0] == BLACK && pic[1] == WHITE);
    memcpy(buf, "P5\n1 1\n65535\n\x80\x00", 15);
    CHECK(read(15, &w, &h, msg) == 0 && pic[0] == 0xFF808080u);
    memcpy(buf, "P6\n1 1\n0\n", 9);
    CHECK(read(12, &w, &h, msg) == -1 && strstr(msg, "bad header"));
    memcpy(buf, "P6 1 1 255", 10);
    CHECK(read(10, &w, &h, msg) == -1 && strstr(msg, "cut short"));

    CHECK(paint_kind_of_name("a.PPM") == PAINT_FILE_PPM && paint_kind_of_name("x/y.pnm") == PAINT_FILE_PPM);
    CHECK(paint_kind_of_name("a.bmp") == PAINT_FILE_BMP && paint_kind_of_name("ppm") == PAINT_FILE_BMP);
}

/* ---- the window ---- */

#define W 960
#define H 680
static uint32_t screen[W * H];
static sqg_canvas cv;
static sqg_ui ui;
static sqg_input in;
static PaintGui g;

static void frame(void) {
    sqg_begin(&ui, &cv, &in);
    paint_gui_frame(&g, &ui);
    sqg_end(&ui);
    in.pressed = in.released = in.pressed2 = in.released2 = 0;
    in.wheel = 0;
    in.key = 0;
    in.ctrl_key = 0;
    in.text[0] = 0;
}

static void move(int x, int y) {
    in.mx = x;
    in.my = y;
    frame();
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

/* the window point of a picture pixel's middle */
static void at(int x, int y, int *sx, int *sy) {
    double s = paint_gui_scale(g.zoom);
    paint_gui_to_screen(&g, x, y, sx, sy);
    *sx += (int)(s / 2);
    *sy += (int)(s / 2);
}

static void drag(int x0, int y0, int x1, int y1, int right) {
    int sx, sy;
    at(x0, y0, &sx, &sy);
    in.mx = sx;
    in.my = sy;
    if (right) in.down2 = in.pressed2 = 1;
    else in.down = in.pressed = 1;
    frame();
    at((x0 + x1) / 2, (y0 + y1) / 2, &sx, &sy);
    move(sx, sy);
    at(x1, y1, &sx, &sy);
    move(sx, sy);
    if (right) in.down2 = 0, in.released2 = 1;
    else in.down = 0, in.released = 1;
    frame();
}

static int opened, saved;
static char seen_path[200];
static int fake_open(PaintGui *pg, const char *path, char *msg, size_t cap) {
    opened++;
    snprintf(seen_path, sizeof seen_path, "%s", path);
    if (strstr(path, "bad")) {
        snprintf(msg, cap, "not a picture this reads");
        return -1;
    }
    paint_new(pg->doc, 30, 20);
    return 0;
}
static int fake_save(PaintGui *pg, const char *path, char *msg, size_t cap) {
    (void)pg;
    saved++;
    snprintf(seen_path, sizeof seen_path, "%s", path);
    if (strstr(path, "bad")) {
        snprintf(msg, cap, "cannot write it");
        return -1;
    }
    return 0;
}

/* where the top bar's buttons are: the layout in paintgui.c */
static int top_x(const char *label) {
    static const char *const order[] = {"Open", "Save", "#w", "x", "#h", "Resize", "New", "Undo", "Redo", "-", "+", "1:1", "Fit"};
    int x = 6 + 180 + 4;
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        if (!strcmp(order[i], label)) return x + 4;
        int wdt = order[i][0] == '#' ? 40 : !strcmp(order[i], "x") ? sqg_text_width("x", 1) : sqg_button_w(&ui, order[i]);
        x += wdt + 4;
        if (!strcmp(order[i], "Save") || !strcmp(order[i], "New") || !strcmp(order[i], "Redo")) x += 4 + 4;
    }
    return -1;
}

static void test_window(const char *dir) {
    sqg_canvas_init(&cv, screen, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    paint_new(&d, 100, 60);
    paint_gui_init(&g, &d);
    g.open_file = fake_open;
    g.save_file = fake_save;
    frame();
    int lh = sqg_line_h(&ui);
    /* a small picture is fitted bigger, and sits in the middle */
    CHECK(g.view_ready && paint_gui_scale(g.zoom) == 4);
    int sx, sy, x, y;
    paint_gui_to_screen(&g, 50, 30, &sx, &sy);
    CHECK(abs(sx - (g.area.x + g.area.w / 2)) <= 2 && abs(sy - (g.area.y + g.area.h / 2)) <= 2);
    paint_gui_to_picture(&g, sx + 1, sy + 1, &x, &y);
    CHECK(x == 50 && y == 30);
    paint_gui_to_picture(&g, g.area.x, g.area.y, &x, &y);
    CHECK(x < 0 && y < 0);
    /* it is drawn: a picture pixel fills scale x scale window pixels */
    d.px[30 * 100 + 50] = RED;
    frame();
    CHECK(screen[(sy + 1) * W + sx + 1] == RED && screen[(sy + 3) * W + sx + 3] == RED && screen[(sy + 5) * W + sx + 5] == WHITE);
    d.px[30 * 100 + 50] = WHITE;

    /* drawing with the mouse: the brush, then a rectangle with the right button */
    d.size = 3;
    drag(10, 10, 40, 10, 0);
    CHECK(paint_get(&d, 25, 10) == BLACK && paint_get(&d, 25, 11) == BLACK && paint_can_undo(&d) && !g.painting);
    int tool_w = (132 - 4) / 2;
    (void)tool_w;
    click(6 + 5, 6 + lh + 6 + 2 * (lh + 2) + 4); /* Rect: the third row, left */
    CHECK(d.tool == PAINT_RECT);
    d.bg = BLUE;
    drag(50, 20, 90, 50, 1);
    CHECK(paint_get(&d, 50, 20) == BLUE && paint_get(&d, 90, 50) == BLUE && paint_get(&d, 70, 35) == WHITE);
    /* Shift holds it square */
    in.shift = 1;
    drag(5, 30, 20, 55, 0);
    in.shift = 0;
    CHECK(paint_get(&d, 30, 55) == BLACK && paint_get(&d, 5, 55) == BLACK);
    /* Ctrl+Z, Ctrl+Y */
    in.ctrl_key = 'z';
    frame();
    CHECK(paint_get(&d, 30, 55) == WHITE && paint_can_redo(&d));
    in.ctrl_key = 'y';
    frame();
    CHECK(paint_get(&d, 30, 55) == BLACK);
    /* the Undo and Redo buttons */
    click(top_x("Undo"), 6 + lh / 2);
    CHECK(paint_get(&d, 30, 55) == WHITE);
    click(top_x("Redo"), 6 + lh / 2);
    CHECK(paint_get(&d, 30, 55) == BLACK && strstr(g.status, "Redone"));
    /* a letter picks a tool; Escape takes back a stroke */
    snprintf(in.text, sizeof in.text, "l");
    frame();
    CHECK(d.tool == PAINT_LINE);
    at(0, 0, &sx, &sy);
    in.mx = sx, in.my = sy;
    in.down = in.pressed = 1;
    frame();
    at(99, 59, &sx, &sy);
    move(sx, sy);
    CHECK(paint_get(&d, 99, 59) == BLACK);
    in.key = SQG_KEY_ESCAPE;
    frame();
    CHECK(paint_get(&d, 99, 59) == WHITE && !d.active && !g.painting);
    in.down = 0, in.released = 1;
    frame();
    CHECK(paint_get(&d, 99, 59) == WHITE);
    /* the palette: left for the first colour, right for the second */
    int pal_y = 6 + lh + 6 + 5 * (lh + 2) + 4 + 4 * (lh + 2) + 6 + lh + (lh + 4) + lh + (lh + 4) + (lh + 8) + 50 + (lh + 2) + 4;
    CHECK(screen[pal_y * W + 6 + 10] == BLACK);
    int cw = (132 - 3 * 2) / 4;
    click(6 + (cw + 2) * 1 + cw / 2, pal_y + (cw / 2 + 6) * 1); /* the second row, second: red */
    CHECK(d.fg == RED);
    in.mx = 6 + cw / 2;
    in.my = pal_y;
    in.down2 = in.pressed2 = 1;
    frame();
    in.down2 = 0, in.released2 = 1;
    frame();
    CHECK(d.bg == BLACK);
    /* the wheel zooms about the pointer: the pixel under it stays (once the
     * picture is bigger than the area, so it is not held in the middle) */
    click(top_x("+"), 6 + lh / 2);
    click(top_x("+"), 6 + lh / 2);
    at(20, 15, &sx, &sy);
    in.mx = sx, in.my = sy;
    int z = g.zoom;
    in.wheel = 1;
    frame();
    CHECK(g.zoom == z + 1);
    paint_gui_to_picture(&g, sx, sy, &x, &y);
    CHECK(x == 20 && y == 15);
    in.wheel = -3; /* a step a frame, however far it turned */
    frame();
    CHECK(g.zoom == z);
    /* Ctrl and a drag move the view */
    click(top_x("+"), 6 + lh / 2);
    click(top_x("+"), 6 + lh / 2);
    click(top_x("+"), 6 + lh / 2);
    double ox = g.ox;
    in.ctrl = 1;
    in.mx = g.area.x + 200, in.my = g.area.y + 200;
    in.down = in.pressed = 1;
    frame();
    move(g.area.x + 100, g.area.y + 200);
    in.down = 0, in.released = 1;
    frame();
    in.ctrl = 0;
    CHECK(g.ox > ox && !g.panning);
    CHECK(count(RED) == 0); /* panning drew nothing */
    ox = g.ox;
    in.key = SQG_KEY_LEFT;
    frame();
    CHECK(g.ox < ox);
    click(top_x("1:1"), 6 + lh / 2);
    CHECK(paint_gui_scale(g.zoom) == 1);
    click(top_x("Fit"), 6 + lh / 2);
    CHECK(paint_gui_scale(g.zoom) == 4);
    /* the status: the size, a star for unsaved, the zoom, the pixel under the pointer */
    at(25, 10, &sx, &sy);
    move(sx, sy);
    CHECK(g.mouse_in && g.mouse_x == 25 && g.mouse_y == 10);
    /* Save, Open, New through the hooks */
    click(top_x("Save"), 6 + lh / 2);
    CHECK(saved == 1 && !strcmp(seen_path, "untitled.bmp") && !d.changed && strstr(g.status, "Saved"));
    snprintf(g.path, sizeof g.path, "bad.bmp");
    in.ctrl_key = 's';
    frame();
    CHECK(saved == 2 && strstr(g.status, "cannot write it"));
    click(top_x("Open"), 6 + lh / 2);
    CHECK(opened == 1 && strstr(g.status, "not a picture") && d.w == 100);
    snprintf(g.path, sizeof g.path, "good.ppm");
    click(top_x("Open"), 6 + lh / 2);
    CHECK(opened == 2 && d.w == 30 && d.h == 20 && strstr(g.status, "Opened good.ppm"));
    frame();
    CHECK(paint_gui_scale(g.zoom) == 16); /* fitted again */
    snprintf(g.new_w, sizeof g.new_w, "200");
    snprintf(g.new_h, sizeof g.new_h, "abc");
    d.tool = PAINT_ELLIPSE;
    click(top_x("New"), 6 + lh / 2);
    CHECK(d.w == 30 && strstr(g.status, "New: a size"));
    snprintf(g.new_h, sizeof g.new_h, "150");
    click(top_x("New"), 6 + lh / 2);
    CHECK(d.w == 200 && d.h == 150 && d.tool == PAINT_ELLIPSE && count(WHITE) == 200 * 150);
    frame();
    drag(20, 20, 120, 100, 0);
    frame();
    char path[512];
    snprintf(path, sizeof path, "%s/paint.ppm", dir);
    CHECK(sqg_write_ppm(&cv, path) == 0);
}


static void test_window_selection(void) {
    int lh = sqg_line_h(&ui), ops_y = 6 + lh + 6 + 5 * (lh + 2) + 4;
    paint_new(&d, 120, 80);
    g.view_ready = 0;
    frame();
    paint_rect(&d, 10, 10, 29, 19, 1, 1, RED);
    snprintf(in.text, sizeof in.text, "s");
    frame();
    CHECK(d.tool == PAINT_SELECT);
    drag(10, 10, 29, 19, 0);
    CHECK(d.sel.x == 10 && d.sel.y == 10 && d.sel.w == 20 && d.sel.h == 10);
    frame();
    CHECK(g.painting == 0 && !d.sel_mode);
    /* the edge is drawn dashed around it */
    int sx, sy;
    paint_gui_to_screen(&g, 10, 10, &sx, &sy);
    CHECK(screen[(sy - 1) * W + sx - 1] == BLACK || screen[(sy - 1) * W + sx - 1] == WHITE);
    in.ctrl_key = 'c';
    frame();
    CHECK(d.clip_w == 20 && strstr(g.status, "Copied"));
    /* drag it by its inside: it moves, floating, shown where it is */
    drag(15, 15, 55, 45, 0);
    CHECK(d.floating && d.sel.x == 50 && d.sel.y == 40 && paint_get(&d, 12, 12) == WHITE);
    paint_gui_to_screen(&g, 52, 42, &sx, &sy);
    CHECK(screen[(sy + 1) * W + sx + 1] == RED);
    /* arrows nudge it */
    in.key = SQG_KEY_RIGHT;
    frame();
    in.key = SQG_KEY_DOWN;
    frame();
    CHECK(d.sel.x == 51 && d.sel.y == 41);
    /* Escape puts it down and clears the selection; Ctrl+Z takes the whole move back */
    in.key = SQG_KEY_ESCAPE;
    frame();
    CHECK(!d.floating && !d.sel.w && paint_get(&d, 51, 41) == RED);
    in.ctrl_key = 'z';
    frame();
    CHECK(paint_get(&d, 10, 10) == RED && paint_get(&d, 51, 41) == WHITE);
    /* paste comes in at the top-left of the view, ready to drag */
    d.tool = PAINT_BRUSH;
    in.ctrl_key = 'v';
    frame();
    CHECK(d.floating && d.tool == PAINT_SELECT && d.sel.x == 0 && d.sel.y == 0 && strstr(g.status, "Pasted"));
    /* Delete takes what floats away */
    in.key = SQG_KEY_DELETE;
    frame();
    CHECK(!d.floating && !d.sel.w && paint_get(&d, 0, 0) == WHITE && count(RED) == 200);
    /* Ctrl+A, then Cut from the panel */
    in.ctrl_key = 'a';
    frame();
    CHECK(d.sel.w == 120 && d.sel.h == 80);
    click(6 + 5, ops_y + lh / 2); /* Cut */
    CHECK(count(RED) == 0 && d.clip_w == 120 && strstr(g.status, "Cut"));
    click(6 + 5, ops_y + lh / 2);
    CHECK(strstr(g.status, "Select something first") != NULL);
    int third = (132 - 8) / 3;
    click(6 + 2 * (third + 4) + 5, ops_y + lh / 2); /* Paste */
    CHECK(d.floating && d.sel.w == 120);
    paint_select_none(&d);
    CHECK(count(RED) == 200);
    /* Rot R on the whole picture: a new shape, fitted again */
    int half = (132 - 4) / 2;
    click(6 + half + 4 + 5, ops_y + 3 * (lh + 2) + lh / 2);
    CHECK(d.w == 80 && d.h == 120 && paint_get(&d, 79 - 10, 10) == RED);
    frame();
    CHECK(g.view_ready);
    click(6 + half + 4 + 5, ops_y + 2 * (lh + 2) + lh / 2); /* Flip V */
    CHECK(paint_get(&d, 69, 109) == RED);
    click(6 + third + 4 + 5, ops_y + (lh + 2) + lh / 2); /* Crop, with nothing selected */
    CHECK(strstr(g.status, "Select what to keep first") && d.w == 80);
    paint_select(&d, 60, 100, 79, 119);
    click(6 + third + 4 + 5, ops_y + (lh + 2) + lh / 2);
    CHECK(d.w == 20 && d.h == 20 && !d.sel.w);
    /* Resize from the size fields */
    snprintf(g.new_w, sizeof g.new_w, "40");
    snprintf(g.new_h, sizeof g.new_h, "30");
    click(top_x("Resize"), 6 + lh / 2);
    CHECK(d.w == 40 && d.h == 30 && strstr(g.status, "The picture is now 40 x 30"));
    snprintf(g.new_w, sizeof g.new_w, "0");
    click(top_x("Resize"), 6 + lh / 2);
    CHECK(d.w == 40 && strstr(g.status, "Resize: a size"));
    /* the status tells the selection */
    paint_select(&d, 1, 2, 4, 6);
    frame();
    CHECK(d.sel.w == 4);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    test_shapes();
    test_tools();
    test_undo();
    test_commands();
    test_selection();
    test_transforms();
    test_selection_commands();
    test_files();
    test_window(dir);
    test_window_selection();
    printf("test_paint: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}

/* libs/sqgui unit tests: primitives, clipping, text, widgets driven by
 * scripted input, all on an in-memory canvas (original code). */
#include "sqgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            fails++;                                                           \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

#define W 320
#define H 200
static uint32_t pix[W * H];
static sqg_canvas cv;

static void clear(uint32_t c)
{
    sqg_canvas_init(&cv, pix, W, H, W);
    sqg_fill(&cv, sqg_rect_make(0, 0, W, H), c);
}

static int count(sqg_rect r, uint32_t c)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            n += sqg_get(&cv, x, y) == c;
    return n;
}

static void test_rects(void)
{
    sqg_rect a = sqg_rect_make(0, 0, 10, 10), b = sqg_rect_make(5, 5, 10, 10);
    sqg_rect i = sqg_rect_intersect(a, b);
    CHECK(i.x == 5 && i.y == 5 && i.w == 5 && i.h == 5);
    i = sqg_rect_intersect(a, sqg_rect_make(20, 20, 5, 5));
    CHECK(i.w == 0 && i.h == 0);
    CHECK(sqg_rect_contains(a, 9, 9) && !sqg_rect_contains(a, 10, 0));
    i = sqg_rect_inset(a, 3);
    CHECK(i.x == 3 && i.w == 4);
    CHECK(sqg_rect_inset(a, 8).w == 0);
}

static void test_fill_clip(void)
{
    const uint32_t red = SQG_RGB(255, 0, 0);
    clear(0xFF000000u);
    sqg_fill(&cv, sqg_rect_make(-5, -5, 15, 15), red);    /* off the edge */
    CHECK(count(sqg_rect_make(0, 0, W, H), red) == 100);
    clear(0xFF000000u);
    sqg_rect old = sqg_clip(&cv, sqg_rect_make(10, 10, 20, 20));
    sqg_fill(&cv, sqg_rect_make(0, 0, W, H), red);
    CHECK(count(sqg_rect_make(0, 0, W, H), red) == 400);
    CHECK(sqg_get(&cv, 9, 10) != red && sqg_get(&cv, 10, 10) == red && sqg_get(&cv, 29, 29) == red);
    cv.clip = old;
    sqg_pixel(&cv, W, 0, red);                             /* ignored */
    sqg_pixel(&cv, -1, 0, red);
    CHECK(sqg_get(&cv, W + 3, 0) == 0);
    clear(0xFF000000u);
    sqg_frame(&cv, sqg_rect_make(10, 10, 5, 4), red);
    CHECK(count(sqg_rect_make(0, 0, W, H), red) == 14);
}

static void test_lines(void)
{
    const uint32_t g = SQG_RGB(0, 255, 0);
    clear(0xFF000000u);
    sqg_line(&cv, 0, 0, 9, 9, g);
    CHECK(count(sqg_rect_make(0, 0, W, H), g) == 10);
    for (int k = 0; k < 10; k++)
        CHECK(sqg_get(&cv, k, k) == g);
    clear(0xFF000000u);
    sqg_line(&cv, 20, 5, 0, 0, g);                         /* reversed, shallow */
    CHECK(count(sqg_rect_make(0, 0, W, H), g) == 21);
    CHECK(sqg_get(&cv, 0, 0) == g && sqg_get(&cv, 20, 5) == g);
    clear(0xFF000000u);
    sqg_line(&cv, 3, 0, 3, 7, g);
    CHECK(count(sqg_rect_make(3, 0, 1, 8), g) == 8);
}

static void test_blend_blit(void)
{
    clear(SQG_RGB(0, 0, 0));
    sqg_blend(&cv, sqg_rect_make(0, 0, 1, 1), 0x80FFFFFFu);
    uint32_t p = sqg_get(&cv, 0, 0);
    CHECK(((p >> 16) & 0xFF) == 128);
    sqg_blend(&cv, sqg_rect_make(1, 0, 1, 1), 0x00FFFFFFu);
    CHECK(sqg_get(&cv, 1, 0) == SQG_RGB(0, 0, 0));
    uint32_t img[4] = { 1, 2, 3, 4 };
    clear(0);
    sqg_blit(&cv, W - 1, 0, img, 2, 2, 2);                 /* half off the right */
    CHECK(sqg_get(&cv, W - 1, 0) == 1 && sqg_get(&cv, W - 1, 1) == 3);
    clear(0);
    sqg_blit_scaled(&cv, sqg_rect_make(10, 10, 4, 4), img, 2, 2, 2);
    CHECK(sqg_get(&cv, 10, 10) == 1 && sqg_get(&cv, 11, 11) == 1 && sqg_get(&cv, 12, 10) == 2 &&
          sqg_get(&cv, 13, 13) == 4);
}

static void test_text(void)
{
    const uint32_t w = SQG_RGB(255, 255, 255);
    CHECK(sqg_text_width("abc", 1) == 18);
    CHECK(sqg_text_width("abc", 2) == 36);
    CHECK(sqg_text_width("ab\nabcd", 1) == 24);
    CHECK(sqg_text_height("ab\nabcd", 2) == 36);
    CHECK(sqg_text_width("\xc3\xa9t\xc3\xa9", 1) == 18);   /* one cell per UTF-8 character */
    clear(0xFF000000u);
    CHECK(sqg_text(&cv, 0, 0, "I", w, 1) == 6);
    /* the font's I: a top bar, a stem, a bottom bar */
    CHECK(count(sqg_rect_make(0, 0, 6, 9), w) > 8);
    CHECK(count(sqg_rect_make(5, 0, 1, 9), w) == 0);       /* spacing column */
    clear(0xFF000000u);
    sqg_text(&cv, 0, 0, "I", w, 3);
    int n3 = count(sqg_rect_make(0, 0, 18, 27), w);
    clear(0xFF000000u);
    sqg_text(&cv, 0, 0, "I", w, 1);
    CHECK(n3 == 9 * count(sqg_rect_make(0, 0, 6, 9), w));
    clear(0xFF000000u);
    sqg_text(&cv, 0, 0, " ", w, 2);
    CHECK(count(sqg_rect_make(0, 0, 12, 18), w) == 0);
}

static void test_bevel(void)
{
    const sqg_theme *t = &sqg_theme_warp;
    clear(0xFF123456u);
    sqg_bevel(&cv, sqg_rect_make(10, 10, 20, 10), 0, 1, t);
    CHECK(sqg_get(&cv, 10, 10) == t->light);              /* raised: light top left */
    CHECK(sqg_get(&cv, 29, 19) == t->dark);               /* dark bottom right */
    CHECK(sqg_get(&cv, 15, 15) == t->face);
    sqg_bevel(&cv, sqg_rect_make(40, 10, 20, 10), 1, 0, t);
    CHECK(sqg_get(&cv, 40, 10) == t->shadow);             /* sunken: shadow top left */
    CHECK(sqg_get(&cv, 59, 19) == t->light);
    CHECK(sqg_get(&cv, 45, 15) == 0xFF123456u);           /* not filled */
}

/* Scripted input */
static sqg_ui ui;
static sqg_input in;

static void frame_begin(void)
{
    sqg_begin(&ui, &cv, &in);
}

static void frame_end(void)
{
    sqg_end(&ui);
    in.pressed = in.released = 0;
    in.wheel = 0;
    in.key = 0;
    in.text[0] = 0;
}

static void move(int x, int y) { in.mx = x; in.my = y; }
static void press(void) { in.down = 1; in.pressed = 1; }
static void release(void) { in.down = 0; in.released = 1; }

static void test_button(void)
{
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    clear(0xFF000000u);
    sqg_rect b = sqg_rect_make(10, 10, 60, 20);
    sqg_id id = sqg_id_str("ok");
    int clicks = 0;

    move(20, 15);
    frame_begin(); clicks += sqg_button(&ui, id, b, "OK"); frame_end();
    CHECK(ui.hot == 0 || clicks == 0);
    press();
    frame_begin(); clicks += sqg_button(&ui, id, b, "OK");
    CHECK(ui.active == id);
    CHECK(sqg_get(&cv, 10, 10) == ui.theme->shadow);       /* drawn pressed */
    frame_end();
    release();
    frame_begin(); clicks += sqg_button(&ui, id, b, "OK"); frame_end();
    CHECK(clicks == 1);
    CHECK(ui.active == 0);
    CHECK(sqg_get(&cv, 10, 10) == ui.theme->light);        /* raised again */

    /* press on it, slide off, release: no click */
    press();
    frame_begin(); clicks += sqg_button(&ui, id, b, "OK"); frame_end();
    move(200, 150);
    release();
    frame_begin(); clicks += sqg_button(&ui, id, b, "OK"); frame_end();
    CHECK(clicks == 1);

    /* press elsewhere, release on it: no click */
    press();
    frame_begin(); clicks += sqg_button(&ui, id, b, "OK"); frame_end();
    move(20, 15);
    release();
    frame_begin(); clicks += sqg_button(&ui, id, b, "OK"); frame_end();
    CHECK(clicks == 1);
}

static void click_at(int x, int y)
{
    move(x, y);
    press();
}

static void test_toggle_checkbox_segments(void)
{
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    clear(0xFF000000u);
    int on = 0, chk = 1, sel = 0;
    const char *labs[] = { "Slow", "Normal", "Fast" };
    sqg_rect tr = sqg_rect_make(0, 0, 50, 20), cr = sqg_rect_make(0, 30, 100, 20);
    sqg_rect sr = sqg_rect_make(0, 60, 150, 20);
    for (int step = 0; step < 6; step++) {
        if (step == 0) click_at(5, 5);
        if (step == 1) release();
        if (step == 2) click_at(5, 35);
        if (step == 3) release();
        if (step == 4) click_at(140, 65);
        if (step == 5) release();
        frame_begin();
        int t = sqg_toggle(&ui, 10, tr, "Hold", &on);
        int c = sqg_checkbox(&ui, 20, cr, "Auto", &chk);
        int s = sqg_segments(&ui, 30, sr, labs, 3, &sel);
        frame_end();
        if (step == 1) CHECK(t && on == 1);
        if (step == 3) CHECK(c && chk == 0);
        if (step == 5) CHECK(s && sel == 2);
        if (step != 1) CHECK(!t);
    }
    /* the toggle is drawn down while on */
    CHECK(sqg_get(&cv, 0, 0) == ui.theme->shadow);
}

static void test_slider(void)
{
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    clear(0xFF000000u);
    double v = 0;
    sqg_rect r = sqg_rect_make(0, 0, 108, 16);              /* knob 8, travel 100 */
    click_at(4, 8);
    frame_begin(); sqg_slider(&ui, 1, r, &v, -100, 0); frame_end();
    CHECK(v == -100);
    move(54, 8);                                            /* drag to the middle */
    frame_begin(); int ch = sqg_slider(&ui, 1, r, &v, -100, 0); frame_end();
    CHECK(ch && v > -51 && v < -49);
    move(500, 8);                                           /* past the end */
    frame_begin(); sqg_slider(&ui, 1, r, &v, -100, 0); frame_end();
    CHECK(v == 0);
    release();
    frame_begin(); sqg_slider(&ui, 1, r, &v, -100, 0); frame_end();
    move(54, 8);
    in.wheel = -2;                                          /* two steps down */
    frame_begin(); sqg_slider(&ui, 1, r, &v, -100, 0); frame_end();
    CHECK(v == -10);
}

static void test_list(void)
{
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    clear(0xFF000000u);
    const char *rows[] = { "zero", "one", "two", "three", "four", "five", "six", "seven" };
    int sel = 0, top = 0;
    sqg_rect r = sqg_rect_make(0, 0, 80, 4 + 3 * 11);       /* three rows visible */
    click_at(10, 2 + 11 + 5);                               /* second row */
    frame_begin(); int ch = sqg_list(&ui, 5, r, rows, 8, &sel, &top); frame_end();
    CHECK(ch && sel == 1);
    release();
    frame_begin(); sqg_list(&ui, 5, r, rows, 8, &sel, &top); frame_end();
    for (int k = 0; k < 4; k++) {
        in.key = SQG_KEY_DOWN;
        frame_begin(); sqg_list(&ui, 5, r, rows, 8, &sel, &top); frame_end();
    }
    CHECK(sel == 5 && top == 3);                            /* scrolled to keep it in view */
    in.key = SQG_KEY_END;
    frame_begin(); sqg_list(&ui, 5, r, rows, 8, &sel, &top); frame_end();
    CHECK(sel == 7 && top == 5);
    move(10, 10);
    in.wheel = 5;
    frame_begin(); sqg_list(&ui, 5, r, rows, 8, &sel, &top); frame_end();
    CHECK(top == 0);
    /* the selected row is filled with the selection colour */
    top = 5;
    frame_begin(); sqg_list(&ui, 5, r, rows, 8, &sel, &top); frame_end();
    CHECK(count(sqg_rect_make(2, 2 + 2 * 11, 76, 11), ui.theme->select) > 0);
}

static void test_textfield(void)
{
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    clear(0xFF000000u);
    char buf[8] = "ab";
    sqg_rect r = sqg_rect_make(0, 0, 100, 16);
    click_at(90, 8);                                        /* focus, cursor at the end */
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    CHECK(ui.focus == 9 && ui.cursor == 2);
    release();
    strcpy(in.text, "cdefghij");                            /* only 5 more fit */
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    CHECK(strcmp(buf, "abcdefg") == 0);
    in.key = SQG_KEY_HOME;
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    in.key = SQG_KEY_DELETE;
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    CHECK(strcmp(buf, "bcdefg") == 0);
    in.key = SQG_KEY_END;
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    in.key = SQG_KEY_BACKSPACE;
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    CHECK(strcmp(buf, "bcdef") == 0);
    in.key = SQG_KEY_ENTER;
    frame_begin(); int e = sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    CHECK(e == 1);
    click_at(200, 150);                                     /* click away: focus lost */
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    release();
    strcpy(in.text, "x");
    frame_begin(); sqg_textfield(&ui, 9, r, buf, sizeof buf); frame_end();
    CHECK(strcmp(buf, "bcdef") == 0);
}

static void test_textbox_and_rows(void)
{
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    clear(0xFF000000u);
    frame_begin();
    int lines = sqg_textbox(&ui, sqg_rect_make(0, 0, 4 + 6 + 10 * 6, 60), "0123456789abcdef\nxy");
    frame_end();
    CHECK(lines == 3);                                      /* 10 columns: wrap, then a newline */
    sqg_row row = sqg_row_begin(sqg_rect_make(0, 0, 100, 20), 4);
    sqg_rect a = sqg_row_take(&row, 30), b = sqg_row_take(&row, 30), c = sqg_row_take(&row, 0);
    CHECK(a.x == 0 && b.x == 34 && c.x == 68 && c.w == 32);
    CHECK(sqg_row_take(&row, 10).w == 0);
}

/* A whole panel: its rendering must stay the same (golden hash) */
static void test_golden(const char *ppm)
{
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    clear(sqg_theme_warp.face);
    int on = 1, chk = 0, sel = 1, lsel = 2, top = 0;
    double v = -40;
    char field[32] = "Squirrel";
    const char *segs[] = { "Slow", "Normal", "Fast" };
    const char *rows[] = { "Shinjuku", "Ikebukuro", "Shibuya", "Ueno" };
    frame_begin();
    sqg_bevel(&cv, sqg_rect_make(0, 0, W, H), 0, 0, ui.theme);
    sqg_label(&ui, sqg_rect_make(8, 6, 200, 14), "sqgui 0.1: The quick brown fox", SQG_ALIGN_LEFT);
    sqg_button(&ui, 1, sqg_rect_make(8, 24, 60, 20), "Start");
    sqg_toggle(&ui, 2, sqg_rect_make(72, 24, 60, 20), "Hold", &on);
    sqg_checkbox(&ui, 3, sqg_rect_make(140, 24, 80, 20), "Auto", &chk);
    sqg_slider(&ui, 4, sqg_rect_make(8, 50, 150, 16), &v, -120, 0);
    sqg_segments(&ui, 5, sqg_rect_make(8, 72, 180, 20), segs, 3, &sel);
    sqg_list(&ui, 10, sqg_rect_make(8, 98, 120, 60), rows, 4, &lsel, &top);
    sqg_textfield(&ui, 11, sqg_rect_make(136, 98, 120, 18), field, sizeof field);
    sqg_textbox(&ui, sqg_rect_make(136, 120, 176, 70), "Decoded: https://example.org/\nOK");
    frame_end();
    if (ppm)
        sqg_write_ppm(&cv, ppm);
    uint32_t h = sqg_hash(&cv);
    printf("golden panel hash %08x\n", h);
#ifdef SQGUI_GOLDEN
    CHECK(h == SQGUI_GOLDEN);
#endif
}

int main(int argc, char **argv)
{
    test_rects();
    test_fill_clip();
    test_lines();
    test_blend_blit();
    test_text();
    test_bevel();
    test_button();
    test_toggle_checkbox_segments();
    test_slider();
    test_list();
    test_textfield();
    test_textbox_and_rows();
    test_golden(argc > 1 ? argv[1] : NULL);
    printf("sqgui: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

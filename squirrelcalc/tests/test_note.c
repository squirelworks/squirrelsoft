/* SquirrelCalc notebooks: the cells, running them, the notebook file, and
 * the notebook view driven off screen. Usage: test_note BUILD_DIR (writes
 * BUILD_DIR/notes.ppm) */
#include "calcgui.h"

#include <math.h>
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

static int contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static CalcNote nb;

static void test_cells(void) {
    calc_note_init(&nb);
    CHECK(nb.n == 0 && nb.sel == -1 && nb.edit_at == -1);
    CHECK(calc_note_insert(&nb, 0, CALC_NOTE_MATH, "a = 1") == 0);
    CHECK(calc_note_insert(&nb, 99, CALC_NOTE_MATH, "c = 3") == 1); /* past the end: at the end */
    CHECK(calc_note_insert(&nb, 1, CALC_NOTE_TEXT, "b") == 1);
    CHECK(calc_note_insert(&nb, -5, CALC_NOTE_TEXT, "first") == 0);
    CHECK(nb.n == 4 && !strcmp(nb.c[0].text, "first") && !strcmp(nb.c[1].text, "a = 1") &&
          !strcmp(nb.c[2].text, "b") && !strcmp(nb.c[3].text, "c = 3"));
    CHECK(calc_note_insert(&nb, 0, 7, "x") == -1);
    CHECK(calc_note_insert(&nb, 0, CALC_NOTE_TEXT, "two\nlines") == -1);
    char big[CALC_NOTE_LEN + 1];
    memset(big, 'x', sizeof big - 1);
    big[CALC_NOTE_LEN] = 0;
    CHECK(calc_note_insert(&nb, 0, CALC_NOTE_TEXT, big) == -1);
    big[CALC_NOTE_LEN - 1] = 0;
    CHECK(calc_note_set(&nb, 0, big) == 0 && strlen(nb.c[0].text) == CALC_NOTE_LEN - 1);
    CHECK(calc_note_set(&nb, 4, "x") == -1 && calc_note_set(&nb, -1, "x") == -1);
    CHECK(calc_note_set(&nb, 0, "first") == 0);

    /* the selection and the edit line's cell follow the cells */
    nb.sel = 2;     /* "b" */
    nb.edit_at = 3; /* "c = 3" */
    CHECK(calc_note_insert(&nb, 0, CALC_NOTE_TEXT, "zero") == 0);
    CHECK(nb.sel == 3 && nb.edit_at == 4 && !strcmp(nb.c[nb.sel].text, "b"));
    CHECK(calc_note_move(&nb, 3, 0) == 0 && nb.sel == 0 && !strcmp(nb.c[0].text, "b"));
    CHECK(nb.edit_at == 4 && !strcmp(nb.c[4].text, "c = 3"));
    CHECK(calc_note_move(&nb, 4, 1) == 1 && nb.edit_at == 1 && nb.sel == 0);
    CHECK(!strcmp(nb.c[0].text, "b") && !strcmp(nb.c[1].text, "c = 3") && !strcmp(nb.c[2].text, "zero") &&
          !strcmp(nb.c[3].text, "first") && !strcmp(nb.c[4].text, "a = 1"));
    CHECK(calc_note_move(&nb, 0, 99) == 4 && nb.sel == 4 && nb.edit_at == 0); /* to the end */
    CHECK(calc_note_move(&nb, 2, 2) == 2 && calc_note_move(&nb, 5, 0) == -1);
    /* taking the selected cell out selects the next, or the new last */
    CHECK(calc_note_remove(&nb, 0) == 0 && nb.edit_at == -1 && nb.sel == 3 && nb.n == 4);
    CHECK(calc_note_remove(&nb, 3) == 0 && nb.sel == 2 && nb.n == 3);
    CHECK(calc_note_remove(&nb, 0) == 0 && nb.sel == 1);
    CHECK(calc_note_remove(&nb, 7) == -1);
    CHECK(calc_note_set_kind(&nb, 0, CALC_NOTE_MATH) == 0 && nb.c[0].kind == CALC_NOTE_MATH);
    CHECK(calc_note_set_kind(&nb, 0, 3) == -1);
    while (nb.n) calc_note_remove(&nb, 0);
    CHECK(nb.sel == -1 && nb.top == 0);
    for (int i = 0; i < CALC_NOTE_MAX; i++) calc_note_insert(&nb, i, CALC_NOTE_MATH, "1");
    CHECK(nb.n == CALC_NOTE_MAX && calc_note_insert(&nb, 0, CALC_NOTE_MATH, "1") == -1);
}

static void test_result(void) {
    CalcNoteCell c = {CALC_NOTE_MATH, "2 + 3", "2 + 3 = 5", 0};
    char r[CALC_NOTE_OUT];
    calc_note_result(&c, r, sizeof r);
    CHECK(!strcmp(r, "= 5"));
    snprintf(c.text, sizeof c.text, "  a = 2 ");
    snprintf(c.out, sizeof c.out, "a = 2");
    calc_note_result(&c, r, sizeof r);
    CHECK(!strcmp(r, "")); /* nothing more to say */
    snprintf(c.text, sizeof c.text, "1 +");
    snprintf(c.out, sizeof c.out, "1 +: unexpected end");
    calc_note_result(&c, r, sizeof r);
    CHECK(!strcmp(r, "unexpected end"));
    snprintf(c.text, sizeof c.text, "stats xs");
    snprintf(c.out, sizeof c.out, "stats xs\n  n = 3");
    calc_note_result(&c, r, sizeof r);
    CHECK(!strcmp(r, "  n = 3"));
    snprintf(c.text, sizeof c.text, "a");
    snprintf(c.out, sizeof c.out, "ab = 3"); /* a longer name is not a copy */
    calc_note_result(&c, r, sizeof r);
    CHECK(!strcmp(r, "ab = 3"));
    c.kind = CALC_NOTE_TEXT;
    calc_note_result(&c, r, sizeof r);
    CHECK(!strcmp(r, ""));

    const char *body;
    CalcNoteCell t = {CALC_NOTE_TEXT, " ## Results", "", 0};
    CHECK(calc_note_heading(&t, &body) == 1 && !strcmp(body, "Results"));
    snprintf(t.text, sizeof t.text, "plain");
    CHECK(calc_note_heading(&t, &body) == 0 && !strcmp(body, "plain"));
    t.kind = CALC_NOTE_MATH;
    snprintf(t.text, sizeof t.text, "# not a heading");
    CHECK(calc_note_heading(&t, &body) == 0);

    CHECK(calc_note_wrap("", 10) == 0);
    CHECK(calc_note_wrap("short", 10) == 5);
    CHECK(calc_note_wrap("one two three", 8) == 8); /* "one two " */
    CHECK(calc_note_wrap("abcdefghijkl", 5) == 5);  /* a long word is broken */
    CHECK(calc_note_wrap("ab\ncd", 10) == 3);       /* to the newline, which goes with it */
    CHECK(calc_note_wrap("ab   cd", 3) == 5);       /* the spaces stay on the first line */
    CHECK(calc_note_wrap("abc", 0) == 1);
}

static CalcGui g;
static char text[1 << 16], text2[1 << 16];

static void test_run(void) {
    calc_gui_init(&g);
    CHECK(calc_gui_notebook_run(&g) == 0);
    calc_note_insert(&g.note, 99, CALC_NOTE_TEXT, "# Waves");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "a = 2");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "a * 3");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "y1 = a sin(x)");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "b + 1");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "   ");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "P = (a, 1)");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "xs = {1, 2, 3}");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "B1 = a + 1");
    CHECK(calc_gui_notebook_run(&g) == 1);
    CHECK(!strcmp(g.note.c[2].out, "a * 3 = 6") && !g.note.c[2].bad);
    CHECK(!strcmp(g.fx[0], "a sin(x)") && g.ok[0] && !strcmp(g.note.c[3].out, "y1 = a sin(x)"));
    CHECK(g.note.c[4].bad && contains(g.note.c[4].out, "b is not defined"));
    CHECK(!g.note.c[5].out[0] && !g.note.c[0].out[0]);
    CHECK(g.sheet.val[0][1] == 3);
    CHECK(!g.hist[0]); /* the notebook's results stay with its cells */
    /* change a line: what follows it is worked out again, from a clean start */
    calc_gui_eval(&g, "b = 7");
    CHECK(contains(g.hist, "b = 7"));
    calc_note_set(&g.note, 1, "a = 5");
    CHECK(calc_gui_notebook_run(&g) == 1); /* b was the calculator's, and is forgotten */
    CHECK(!strcmp(g.note.c[2].out, "a * 3 = 15") && g.sheet.val[0][1] == 6);
    CHECK(g.geom.o[calc_geom_find(&g.geom, "P")].x == 5 && g.nlists == 1);
    calc_note_remove(&g.note, 1);
    CHECK(calc_gui_notebook_run(&g) == 5); /* a * 3, y1, b + 1, P and B1 */
    CHECK(isnan(calc_gui_var(&g, "a")) && !g.geom.o[calc_geom_find(&g.geom, "P")].ok && g.nlists == 1);
    CHECK(!g.ok[0] && g.sheet.bad[0][1]); /* y1 and B1 lost a with it */
    /* y1 = in the calculator, and its errors */
    CHECK(calc_gui_eval(&g, "y2 = x^2") == 0 && !strcmp(g.fx[1], "x^2") && contains(g.hist, "y2 = x^2"));
    CHECK(calc_gui_eval(&g, "y3 = (x, t)") == -1 && contains(g.hist, "curves use t"));
    CHECK(calc_gui_eval(&g, "y5 = 3") == 0 && calc_gui_var(&g, "y5") == 3); /* only y1..y4 are rows */
    calc_gui_forget(&g);
    CHECK(g.nvars == 0 && g.nlists == 0 && g.geom.n == 0 && !strcmp(g.fx[1], "x^2"));
    /* a long result is cut to what fits */
    calc_note_init(&g.note);
    calc_note_insert(&g.note, 0, CALC_NOTE_MATH, "xs = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}");
    calc_note_insert(&g.note, 1, CALC_NOTE_MATH, "stats xs");
    calc_note_insert(&g.note, 2, CALC_NOTE_MATH, "factor x^4 - 1");
    CHECK(calc_gui_notebook_run(&g) == 0);
    CHECK(strlen(g.note.c[1].out) < CALC_NOTE_OUT && contains(g.note.c[1].out, "mean = 8"));
    CHECK(contains(g.note.c[2].out, "(x - 1)*(x + 1)*(x^2 + 1)"));
    size_t n = calc_gui_notebook_text(&g, text, sizeof text);
    CHECK(n == strlen(text) && contains(text, "> stats xs\n") && contains(text, "  = (x - 1)"));
    CHECK(calc_gui_notebook_text(&g, text, 10) == n && strlen(text) == 9);
}

static void test_file(void) {
    calc_gui_init(&g);
    calc_gui_set(&g, 0, "a x");
    calc_gui_set(&g, 2, "x^2 + y^2 = 25");
    calc_gui_cell(&g, "A1", "5");
    calc_gui_cell(&g, "B2", "=A1*2");
    calc_gui_cell(&g, "C3", "label: with a colon");
    g.view = (CalcView){-3, 3.5, -2, 2};
    g.view_ready = 1;
    calc_note_insert(&g.note, 99, CALC_NOTE_TEXT, "# Lines");
    calc_note_insert(&g.note, 99, CALC_NOTE_TEXT, "notes: with a colon");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "a = B2 / 2");
    calc_note_insert(&g.note, 99, CALC_NOTE_MATH, "");
    size_t n = calc_gui_notebook_save(&g, text, sizeof text);
    CHECK(n == strlen(text));
    CHECK(!strncmp(text, "SquirrelCalc notebook 1\ny1: a x\ny3: x^2 + y^2 = 25\nview: -3 3.5 -2 2\n", 66));
    CHECK(contains(text, "cell A1: 5\ncell B2: =A1*2\ncell C3: label: with a colon\n"));
    CHECK(contains(text, "text: # Lines\ntext: notes: with a colon\nmath: a = B2 / 2\nmath: \n"));
    CHECK(calc_gui_notebook_save(&g, text2, 20) == n && strlen(text2) == 19);

    /* back in: the same session, run */
    char msg[160];
    calc_gui_init(&g);
    calc_gui_set(&g, 3, "old");
    CHECK(calc_gui_notebook_load(&g, text, msg, sizeof msg) == 0);
    CHECK(!strcmp(msg, "4 cells, 0 with an error"));
    CHECK(!g.fx[3][0] && !strcmp(g.fx[0], "a x") && g.ok[0] && calc_gui_var(&g, "a") == 5);
    CHECK(g.view_ready && g.view.xmin == -3 && g.view.xmax == 3.5 && g.view.ymax == 2);
    CHECK(!strcmp(g.sheet.text[2][2], "label: with a colon") && g.sheet.val[1][1] == 10);
    CHECK(g.note.n == 4 && g.note.c[3].kind == CALC_NOTE_MATH && !g.note.c[3].text[0] && g.note.sel == 0);
    CHECK(!strcmp(g.note.c[1].text, "notes: with a colon"));
    CHECK(calc_gui_notebook_save(&g, text2, sizeof text2) == n && !strcmp(text, text2));

    /* comments, blank lines, CRLF, spaces before a line */
    CHECK(calc_gui_notebook_load(&g, "# made by hand\r\n\r\n  SquirrelCalc notebook 1  \r\n# hi\r\n  math: 1 + 1\r\n"
                                     "text:no space\r\n",
                                 msg, sizeof msg) == 0);
    CHECK(g.note.n == 2 && !strcmp(g.note.c[0].text, "1 + 1") && !strcmp(g.note.c[0].out, "1 + 1 = 2"));
    CHECK(!strcmp(g.note.c[1].text, "no space") && !g.view_ready && !g.fx[0][0] && !g.sheet.text[0][0][0]);

    /* errors change nothing */
    calc_gui_notebook_load(&g, text, msg, sizeof msg);
    struct {
        const char *file, *why;
    } bad[] = {
        {"", "not a SquirrelCalc notebook"},
        {"math: 1\n", "not a SquirrelCalc notebook"},
        {"SquirrelCalc notebook 2\n", "not a SquirrelCalc notebook"},
        {"SquirrelCalc notebook 1\nmath: 1\nnonsense\n", "line 3: expected text:"},
        {"SquirrelCalc notebook 1\nwhat: 1\n", "line 2: what: expected"},
        {"SquirrelCalc notebook 1\ny5: x\n", "line 2: y5: expected"},
        {"SquirrelCalc notebook 1\ncell Z9: 1\n", "line 2: cell Z9: not a cell"},
        {"SquirrelCalc notebook 1\ncell A1x: 1\n", "line 2: cell A1x: not a cell"},
        {"SquirrelCalc notebook 1\nview: 1 0 0 1\n", "line 2: view: expected"},
        {"SquirrelCalc notebook 1\nview: 0 1 0\n", "line 2: view: expected"},
        {"SquirrelCalc notebook 1\nview: 0 1 0 nan\n", "line 2: view: expected"},
    };
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        msg[0] = 0;
        int rc = calc_gui_notebook_load(&g, bad[k].file, msg, sizeof msg);
        check(rc == -1 && contains(msg, bad[k].why), bad[k].file, __LINE__);
        if (rc != -1 || !contains(msg, bad[k].why)) fprintf(stderr, "  said: %s\n", msg);
    }
    /* too many cells, a cell too long: found before anything changes */
    char *p = text2;
    p += sprintf(p, "SquirrelCalc notebook 1\n");
    for (int i = 0; i <= CALC_NOTE_MAX; i++) p += sprintf(p, "math: %d\n", i);
    CHECK(calc_gui_notebook_load(&g, text2, msg, sizeof msg) == -1 && contains(msg, "at most 48 cells"));
    p = text2 + sprintf(text2, "SquirrelCalc notebook 1\ntext: ");
    memset(p, 'w', CALC_NOTE_LEN);
    p[CALC_NOTE_LEN] = 0;
    CHECK(calc_gui_notebook_load(&g, text2, msg, sizeof msg) == -1 && contains(msg, "line 2: a cell is at most"));
    p = text2 + sprintf(text2, "SquirrelCalc notebook 1\ny1: ");
    memset(p, 'x', 170);
    p[170] = 0;
    CHECK(calc_gui_notebook_load(&g, text2, msg, sizeof msg) == -1 && contains(msg, "a row is at most 159"));
    CHECK(g.note.n == 4 && !strcmp(g.fx[0], "a x") && g.view.xmax == 3.5); /* as loaded before */
}

/* ---- the view, off screen ---- */

#define W 900
#define H 600
static uint32_t px[W * H];
static sqg_canvas cv;
static sqg_ui ui;
static sqg_input in;

static void frame(void) {
    sqg_begin(&ui, &cv, &in);
    calc_gui_frame(&g, &ui);
    sqg_end(&ui);
    in.pressed = in.released = 0;
    in.wheel = 0;
    in.key = 0;
    in.text[0] = 0;
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

static void type(const char *t) {
    snprintf(in.text, sizeof in.text, "%s", t);
    frame();
}

static void key(int k) {
    in.key = k;
    frame();
}

/* the notebook's toolbar: the window's layout in calcgui.c */
static int lh, gx, top;
static void button(int k) {
    static const char *const labels[] = {"+ Math", "+ Text", "Math/Text", "Up", "Down", "Delete", "Run"};
    int x = gx;
    for (int i = 0; i < k; i++) x += sqg_button_w(&ui, labels[i]) + 4;
    click(x + 5, top + lh / 2);
}
static void click_edit(void) { click(gx + 80, top + lh + 4 + lh / 2); }

static int count(sqg_rect r, uint32_t col) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) n += px[y * W + x] == col;
    return n;
}

static void test_view(const char *dir) {
    calc_gui_init(&g);
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    lh = sqg_line_h(&ui);
    int pad = 6, left_w = W / 3;
    gx = pad + left_w + pad;
    top = pad + lh + 4;
    g.show_notes = 1;
    frame();
    CHECK(g.note_at[0].h == 0);

    /* the calculator line, with the notebook shown, adds to it */
    int input_y = H - pad - lh - pad - lh;
    click(pad + 20, input_y + lh / 2);
    type("a = 2");
    key(SQG_KEY_ENTER);
    CHECK(g.note.n == 1 && !strcmp(g.note.c[0].text, "a = 2") && g.note.sel == 0 && !g.line[0]);
    CHECK(calc_gui_var(&g, "a") == 2 && !g.hist[0]);

    /* + Math, typing, Enter: in, and a fresh line after the last */
    button(0);
    CHECK(g.note.n == 2 && g.note.sel == 1 && ui.focus == sqg_id_str("note"));
    type("a * 3");
    key(SQG_KEY_ENTER);
    CHECK(!strcmp(g.note.c[1].out, "a * 3 = 6"));
    CHECK(g.note.n == 3 && g.note.sel == 2 && !g.note.edit[0]);
    /* Up to the first line, change it: the rest follows */
    key(SQG_KEY_UP);
    key(SQG_KEY_UP);
    CHECK(g.note.sel == 0 && !strcmp(g.note.edit, "a = 2"));
    snprintf(g.note.edit, sizeof g.note.edit, "a = 5");
    key(SQG_KEY_ENTER);
    CHECK(!strcmp(g.note.c[1].out, "a * 3 = 15") && g.note.sel == 1);
    /* a click picks a cell */
    frame();
    CHECK(g.note_at[0].h > 0 && g.note_at[2].h > 0 && g.note_at[2].y > g.note_at[1].y);
    click(g.note_at[0].x + 40, g.note_at[0].y + 4);
    CHECK(g.note.sel == 0 && !strcmp(g.note.edit, "a = 5"));
    /* Down moves it below a * 3, which then has no a */
    button(4);
    CHECK(g.note.sel == 1 && !strcmp(g.note.c[1].text, "a = 5") && g.note.c[0].bad);
    button(3);
    CHECK(g.note.sel == 0 && !g.note.c[1].bad && !strcmp(g.note.c[1].out, "a * 3 = 15"));
    /* Math/Text makes it a line of notes */
    button(2);
    CHECK(g.note.c[0].kind == CALC_NOTE_TEXT && g.note.c[1].bad);
    button(2);
    CHECK(g.note.c[0].kind == CALC_NOTE_MATH && !g.note.c[1].bad);
    /* + Text after it, a heading */
    button(1);
    CHECK(g.note.sel == 1 && g.note.c[1].kind == CALC_NOTE_TEXT);
    type("# Results");
    key(SQG_KEY_ENTER);
    CHECK(!strcmp(g.note.c[1].text, "# Results") && g.note.sel == 2); /* not a new line: on to the next */
    frame();
    CHECK(g.note_at[1].h > 2 * SQG_GLYPH_H); /* drawn big */
    CHECK(count(g.note_at[1], 0xFF202830u) > 40);
    CHECK(count(g.note_at[2], 0xFF207040u) > 10); /* a result */
    /* what is typed and not entered goes in when the view goes */
    click_edit();
    type(" + 1");
    CHECK(!strcmp(g.note.edit, "a * 3 + 1"));
    click(gx + sqg_button_w(&ui, "-") * 2 + sqg_button_w(&ui, "Reset") + sqg_button_w(&ui, "Square") +
              sqg_button_w(&ui, "Table") + sqg_button_w(&ui, "Sheet") + 6 * 4 + 5,
          pad + lh / 2);
    CHECK(!g.show_notes && !strcmp(g.note.c[2].text, "a * 3 + 1") && !strcmp(g.note.c[2].out, "a * 3 + 1 = 16"));
    g.show_notes = 1;
    frame();
    /* Delete takes the selected cell; Run works it all out again */
    int n = g.note.n;
    button(5);
    CHECK(g.note.n == n - 1 && g.note.sel == 2);
    calc_gui_eval(&g, "a = 100");
    button(6);
    CHECK(calc_gui_var(&g, "a") == 5);
    /* scrolling a long notebook, and following the selection */
    for (int i = 0; i < 40; i++) {
        char t[32];
        snprintf(t, sizeof t, "a + %d", i);
        calc_note_insert(&g.note, 99, CALC_NOTE_MATH, t);
    }
    calc_gui_notebook_run(&g);
    frame();
    CHECK(g.note.top == 0 && g.note_at[g.note.n - 1].h == 0);
    in.mx = g.note_at[0].x + 20;
    in.my = g.note_at[0].y + 4;
    in.wheel = -5;
    frame();
    CHECK(g.note.top == 5);
    in.mx = in.my = -1;
    g.note.sel = g.note.n - 1;
    frame();
    frame();
    CHECK(g.note.top > 5 && g.note_at[g.note.n - 1].h > 0);
    char path[512];
    snprintf(path, sizeof path, "%s/notes.ppm", dir);
    CHECK(sqg_write_ppm(&cv, path) == 0);
    /* the empty notebook says what to do */
    calc_gui_init(&g);
    g.show_notes = 1;
    frame();
    CHECK(count(sqg_rect_make(gx, top, W - gx, H - top), 0xFF707070u) > 100);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    test_cells();
    test_result();
    test_run();
    test_file();
    test_view(dir);
    printf("test_note: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}

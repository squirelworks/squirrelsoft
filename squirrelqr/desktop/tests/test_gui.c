/* SquirrelQR window panel (qrgui), drawn off screen: open a fixture image
 * written by test_desktop, check the text, the outline and the buttons.
 * Usage: test_gui BUILD_DIR (writes BUILD_DIR/qrgui.ppm) */
#include "qrdecode.h"
#include "qrgui.h"
#include "qrtext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 560
#define H 460
#define COL_OUTLINE 0xFF20D060u

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
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
static QrGui g;

static int frame(void) {
    sqg_begin(&ui, &cv, &in);
    int act = qr_gui_frame(&g, &ui);
    sqg_end(&ui);
    in.pressed = in.released = 0;
    in.key = 0;
    in.text[0] = 0;
    return act;
}

/* press and release over (x, y); returns the action of the release frame */
static int click(int x, int y) {
    in.mx = x;
    in.my = y;
    in.down = in.pressed = 1;
    int a = frame();
    in.down = 0;
    in.released = 1;
    int b = frame();
    return b ? b : a;
}

static int count(sqg_rect r, uint32_t col) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) n += px[y * W + x] == col;
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s build-dir\n", argv[0]);
        return 2;
    }
    char fixture[600], blank[600], snap[600];
    snprintf(fixture, sizeof fixture, "%s/fmt_P5.img", argv[1]);
    snprintf(blank, sizeof blank, "%s/gui_blank.pgm", argv[1]);
    snprintf(snap, sizeof snap, "%s/qrgui.ppm", argv[1]);

    /* the expected text, decoded without the panel */
    QrImage ref;
    char err[160];
    static SqqrResult rres;
    static char want[4 * SQQR_MAX_PAYLOAD + 1];
    if (qr_image_load(fixture, &ref, err, sizeof err) || qr_decode_gray(&ref, &rres) != SQQR_OK) {
        fprintf(stderr, "%s: run test_desktop first (%s)\n", fixture, err);
        return 2;
    }
    qr_payload_text(rres.data, rres.len, rres.eci, rres.has_kanji, want, sizeof want);
    qr_image_free(&ref);

    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    qr_gui_init(&g);
    int lh = sqg_line_h(&ui), pad = 6, ty = pad + lh / 2;

    /* empty: a hint, no image, Copy does nothing */
    CHECK(frame() == QR_GUI_NONE);
    CHECK(g.preview_at.w == 0);
    int x = pad;
    int sel_x = x + sqg_button_w(&ui, "Select area") / 2;
    x += sqg_button_w(&ui, "Select area") + 6;
    int scr_x = x + sqg_button_w(&ui, "Whole screen") / 2;
    x += sqg_button_w(&ui, "Whole screen") + 6;
    int copy_x = x + sqg_button_w(&ui, "Copy text") / 2;
    CHECK(click(copy_x, ty) == QR_GUI_NONE);
    CHECK(click(sel_x, ty) == QR_GUI_SELECT);
    CHECK(click(scr_x, ty) == QR_GUI_SCREEN);

    /* open by typing the path and pressing Enter */
    int fy = pad + lh + pad + lh / 2;
    int field_x = pad + sqg_text_width("File", 1) + 4 + 6 + 20;
    click(field_x, fy);
    CHECK(ui.focus != 0);
    snprintf(in.text, sizeof in.text, "%.31s", fixture);
    if (strlen(fixture) < sizeof in.text) {
        frame();
    } else { /* long build paths: type in pieces */
        in.text[0] = 0;
        for (size_t i = 0; i < strlen(fixture); i += 16) {
            snprintf(in.text, sizeof in.text, "%.16s", fixture + i);
            frame();
        }
    }
    CHECK(strcmp(g.path, fixture) == 0);
    in.key = SQG_KEY_ENTER;
    frame();
    CHECK(g.found == 1);
    CHECK(strcmp(g.text, want) == 0);
    CHECK(strstr(g.status, "version") != NULL);
    frame();
    CHECK(g.preview_at.w > 0 && g.preview_at.h > 0);
    CHECK(g.preview_at.w * g.img.h / g.img.w - g.preview_at.h <= 1); /* aspect kept */
    CHECK(count(g.preview_at, COL_OUTLINE) > 40);
    CHECK(click(copy_x, ty) == QR_GUI_COPY);

    /* the Open button does the same */
    int open_x = W - pad - sqg_button_w(&ui, "Open") / 2;
    g.found = 0;
    click(open_x, fy);
    CHECK(g.found == 1);

    /* no code in the picture: text cleared, the reason shown, no outline */
    FILE *f = fopen(blank, "wb");
    if (f) {
        fprintf(f, "P5\n64 48\n255\n");
        for (int i = 0; i < 64 * 48; i++) fputc(i % 7 ? 255 : 0, f);
        fclose(f);
    }
    CHECK(qr_gui_open(&g, blank) == 0);
    CHECK(g.found == 0 && g.text[0] == 0);
    CHECK(strstr(g.status, blank) != NULL);
    frame();
    CHECK(count(g.preview_at, COL_OUTLINE) == 0);
    CHECK(click(copy_x, ty) == QR_GUI_NONE);

    /* unreadable file: status says so, the last image stays */
    CHECK(qr_gui_open(&g, "/nonexistent/x.png") == -1);
    CHECK(strstr(g.status, "/nonexistent/x.png") != NULL);
    CHECK(g.img.gray != NULL);

    /* caller messages */
    qr_gui_status(&g, "Cancelled");
    CHECK(strcmp(g.status, "Cancelled") == 0);

    /* a tiny window draws without the preview and does not crash */
    static uint32_t small[120 * 60];
    sqg_canvas sc;
    sqg_canvas_init(&sc, small, 120, 60, 120);
    sqg_begin(&ui, &sc, &in);
    qr_gui_frame(&g, &ui);
    sqg_end(&ui);

    /* last frame for a look */
    qr_gui_open(&g, fixture);
    in.mx = in.my = -1;
    frame();
    CHECK(sqg_write_ppm(&cv, snap) == 0);

    qr_gui_free(&g);
    printf("test_gui: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

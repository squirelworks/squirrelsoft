/* Tests for the SquirrelQR desktop reader: image loaders, payload text, and
 * (with --x11, under Xvfb) screen capture of a window showing a symbol.
 * Usage: test_desktop [--x11] fixtures/symbols.txt BUILD_DIR */
#include "qrcapture.h"
#include "qrimage.h"
#include "qrtext.h"
#include "sqqr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef QR_TEST_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <time.h>
#endif

static int failures = 0;
#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

typedef struct {
    int n;
    uint8_t grid[SQQR_MAX_SIZE * SQQR_MAX_SIZE];
    size_t len;
    uint8_t want[4000];
} Symbol;

static Symbol syms[64];
static int nsyms;

static int hexval(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

static int load_fixture(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[8192], hex[8000];
    while (fgets(line, sizeof line, f) && nsyms < 64) {
        int version, mask;
        char level;
        if (line[0] != 'S' || sscanf(line, "S %d %c %d %7999s", &version, &level, &mask, hex) != 4) continue;
        Symbol *s = &syms[nsyms++];
        s->n = 17 + 4 * version;
        for (int r = 0; r < s->n; r++) {
            if (!fgets(line, sizeof line, f)) return -1;
            for (int c = 0; c < s->n; c++) s->grid[r * s->n + c] = (hexval(line[c / 4]) >> (3 - c % 4)) & 1;
        }
        s->len = strlen(hex) / 2;
        for (size_t i = 0; i < s->len; i++) s->want[i] = (uint8_t)(hexval(hex[2 * i]) << 4 | hexval(hex[2 * i + 1]));
    }
    fclose(f);
    return nsyms;
}

/* Upright symbol, 4-module quiet zone, scale px per module, on light gray. */
static QrImage render(const Symbol *s, int scale) {
    QrImage im;
    int side = (s->n + 8) * scale;
    im.w = im.h = side;
    im.gray = malloc((size_t)side * side);
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++) {
            int r = y / scale - 4, c = x / scale - 4;
            int dark = r >= 0 && c >= 0 && r < s->n && c < s->n && s->grid[r * s->n + c];
            im.gray[(size_t)y * side + x] = dark ? 20 : 235;
        }
    return im;
}

static int decode(const QrImage *im, SqqrResult *res) {
    size_t need = SQQR_IMAGE_WORK(im->w, im->h);
    void *work = malloc(need);
    int rc = sqqr_decode_image(im->gray, im->w, im->h, im->w, work, need, res);
    free(work);
    return rc;
}

static int matches(const Symbol *s, const SqqrResult *res) {
    return res->len == s->len && memcmp(res->data, s->want, s->len) == 0;
}

static void put16(FILE *f, unsigned v) {
    fputc(v & 255, f);
    fputc(v >> 8 & 255, f);
}
static void put32(FILE *f, unsigned v) {
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

/* Write im in one of several encodings; colour formats tint dark/light so the
 * luma conversion is exercised. */
static void write_as(const char *path, const QrImage *im, const char *fmt) {
    FILE *f = fopen(path, "wb");
    int w = im->w, h = im->h;
    if (!strcmp(fmt, "P5")) {
        fprintf(f, "P5\n# comment\n%d %d\n255\n", w, h);
        fwrite(im->gray, 1, (size_t)w * h, f);
    } else if (!strcmp(fmt, "P5-16")) {
        fprintf(f, "P5 %d %d 65535\n", w, h);
        for (int i = 0; i < w * h; i++) {
            fputc(im->gray[i], f);
            fputc(im->gray[i], f);
        }
    } else if (!strcmp(fmt, "P2")) {
        fprintf(f, "P2\n%d %d\n15\n", w, h);
        for (int i = 0; i < w * h; i++) fprintf(f, "%d%c", im->gray[i] / 17, (i + 1) % w ? ' ' : '\n');
    } else if (!strcmp(fmt, "P6") || !strcmp(fmt, "P3")) {
        int bin = fmt[1] == '6';
        fprintf(f, "P%c\n%d %d\n255\n", fmt[1], w, h);
        for (int i = 0; i < w * h; i++) {
            int g = im->gray[i], r = g < 128 ? 10 : 250, gg = g < 128 ? 40 : 240, b = g < 128 ? 120 : 200;
            if (bin) {
                fputc(r, f);
                fputc(gg, f);
                fputc(b, f);
            } else {
                fprintf(f, "%d %d %d\n", r, gg, b);
            }
        }
    } else if (!strcmp(fmt, "P4") || !strcmp(fmt, "P1")) {
        fprintf(f, "P%c\n%d %d\n", fmt[1], w, h);
        if (fmt[1] == '4') {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x += 8) {
                    int byte = 0;
                    for (int k = 0; k < 8; k++)
                        if (x + k < w && im->gray[y * w + x + k] < 128) byte |= 0x80 >> k;
                    fputc(byte, f);
                }
        } else {
            for (int i = 0; i < w * h; i++) fputs(im->gray[i] < 128 ? "1" : "0", f); /* packed, no spaces */
        }
    } else { /* BMP: "bmp24", "bmp32", "bmp8", "bmp1", "bmp16" (565 bitfields); bottom-up except bmp32 */
        int bpp = atoi(fmt + 3);
        int pal = bpp <= 8 ? 1 << bpp : 0;
        int bf = bpp == 16;
        unsigned row = ((unsigned)(w * bpp) + 31) / 32 * 4;
        unsigned off = 14 + 40 + (bf ? 12 : 0) + pal * 4;
        fputs("BM", f);
        put32(f, off + row * h);
        put32(f, 0);
        put32(f, off);
        put32(f, 40);
        put32(f, (unsigned)w);
        put32(f, bpp == 32 ? (unsigned)-h : (unsigned)h);
        put16(f, 1);
        put16(f, bpp);
        put32(f, bf ? 3 : 0);
        put32(f, row * h);
        put32(f, 2835);
        put32(f, 2835);
        put32(f, (unsigned)pal);
        put32(f, 0);
        if (bf) {
            put32(f, 0xF800);
            put32(f, 0x07E0);
            put32(f, 0x001F);
        }
        for (int i = 0; i < pal; i++) { /* grayscale ramp, entry 0 black */
            int v = pal == 2 ? i * 255 : i * 255 / (pal - 1);
            fputc(v, f);
            fputc(v, f);
            fputc(v, f);
            fputc(0, f);
        }
        uint8_t *line = calloc(row, 1);
        for (int yy = 0; yy < h; yy++) {
            int y = bpp == 32 ? yy : h - 1 - yy;
            memset(line, 0, row);
            for (int x = 0; x < w; x++) {
                int g = im->gray[y * w + x];
                switch (bpp) {
                case 1:
                    if (g >= 128) line[x / 8] |= 0x80 >> (x % 8);
                    break;
                case 8: line[x] = (uint8_t)g; break;
                case 16: {
                    unsigned p = (unsigned)(g >> 3) << 11 | (unsigned)(g >> 2) << 5 | (unsigned)(g >> 3);
                    line[2 * x] = p & 255;
                    line[2 * x + 1] = p >> 8;
                    break;
                }
                case 24: line[3 * x] = line[3 * x + 1] = line[3 * x + 2] = (uint8_t)g; break;
                default:
                    line[4 * x] = line[4 * x + 1] = line[4 * x + 2] = (uint8_t)g;
                    line[4 * x + 3] = 255;
                }
            }
            fwrite(line, 1, row, f);
        }
        free(line);
    }
    fclose(f);
}

static void test_formats(const char *dir) {
    static const char *fmts[] = {"P5", "P5-16", "P2", "P6", "P3", "P4", "P1", "bmp24", "bmp32", "bmp8", "bmp1", "bmp16"};
    for (int i = 0; i < nsyms && i < 12; i++) { /* versions 1..10, one format each, then wrap */
        const Symbol *s = &syms[(i * 5) % nsyms];
        QrImage im = render(s, 3 + i % 3), back;
        char path[512], err[160];
        snprintf(path, sizeof path, "%s/fmt_%s.img", dir, fmts[i]);
        write_as(path, &im, fmts[i]);
        int rc = qr_image_load(path, &back, err, sizeof err);
        CHECK(rc == 0, "%s: load: %s", fmts[i], err);
        if (rc == 0) {
            CHECK(back.w == im.w && back.h == im.h, "%s: size %dx%d", fmts[i], back.w, back.h);
            int maxdiff = 0;
            for (int k = 0; k < im.w * im.h && back.w == im.w && back.h == im.h; k++) {
                int a = im.gray[k] < 128, b = back.gray[k] < 128;
                if (a != b) maxdiff = 1;
            }
            CHECK(!maxdiff, "%s: pixels differ in polarity", fmts[i]);
            SqqrResult res;
            rc = decode(&back, &res);
            CHECK(rc == SQQR_OK && matches(s, &res), "%s: decode rc=%d", fmts[i], rc);
            qr_image_free(&back);
        }
        free(im.gray);
    }
    /* Bad inputs fail cleanly. */
    QrImage bad;
    char err[160] = "";
    CHECK(qr_image_decode((const uint8_t *)"P5\n10 10\n255\nxx", 16, &bad, err, sizeof err) < 0, "short P5");
    CHECK(qr_image_decode((const uint8_t *)"P5\n99999 1\n255\n", 16, &bad, err, sizeof err) < 0, "huge P5");
    CHECK(qr_image_decode((const uint8_t *)"\x89PNG\r\n\x1a\n", 8, &bad, err, sizeof err) < 0 && strstr(err, "PNG"),
          "PNG message: %s", err);
    CHECK(qr_image_decode((const uint8_t *)"BM", 2, &bad, err, sizeof err) < 0, "short BMP");
    CHECK(qr_image_decode((const uint8_t *)"GIF89a", 6, &bad, err, sizeof err) < 0, "GIF");
    CHECK(qr_image_load("/nonexistent/x.pgm", &bad, err, sizeof err) < 0, "missing file");
}

static void test_text(void) {
    char out[256];
    const char *cs;
    cs = qr_payload_text((const uint8_t *)"hello", 5, -1, 0, out, sizeof out);
    CHECK(!strcmp(cs, "UTF-8") && !strcmp(out, "hello"), "ascii: %s %s", cs, out);
    const uint8_t latin[] = {'c', 0xE9, '!'};
    cs = qr_payload_text(latin, 3, 3, 0, out, sizeof out);
    CHECK(!strcmp(cs, "ISO-8859-1") && !strcmp(out, "c\xC3\xA9!"), "eci 3: %s", out);
    const uint8_t utf8[] = {0xE6, 0xAE, 0x8B, 0xE9, 0xAB, 0x98};
    cs = qr_payload_text(utf8, 6, -1, 0, out, sizeof out);
    CHECK(!strcmp(cs, "UTF-8") && !memcmp(out, utf8, 6) && !out[6], "utf8");
    const uint8_t sjis[] = {0x8A, 0xBF, 0x8E, 0x9A}; /* 漢字 */
    cs = qr_payload_text(sjis, 4, -1, 0, out, sizeof out);
#ifdef QR_HAS_ICONV
    CHECK(!strcmp(cs, "Shift_JIS") && !strcmp(out, "\xE6\xBC\xA2\xE5\xAD\x97"), "sjis: %s %s", cs, out);
#else
    CHECK(!strcmp(cs, "ISO-8859-1"), "sjis without iconv: %s", cs);
#endif
    const uint8_t junk[] = {0xFF, 0xFE, 0x41};
    cs = qr_payload_text(junk, 3, -1, 0, out, sizeof out);
    CHECK(!strcmp(cs, "ISO-8859-1") && qr_utf8_valid((const uint8_t *)out, strlen(out)), "junk: %s", cs);

    CHECK(!qr_utf8_valid((const uint8_t *)"\xC0\x80", 2), "overlong");
    CHECK(!qr_utf8_valid((const uint8_t *)"\xED\xA0\x80", 3), "surrogate");
    CHECK(!qr_utf8_valid((const uint8_t *)"\xE6\xAE", 2), "truncated");
    CHECK(qr_utf8_valid((const uint8_t *)"\xF0\x9F\x90\xBF", 4), "emoji");

    size_t need = qr_json_string("a\"b\\c\nd\x01", NULL, 0);
    char js[64];
    qr_json_string("a\"b\\c\nd\x01", js, sizeof js);
    CHECK(!strcmp(js, "\"a\\\"b\\\\c\\nd\\u0001\"") && need == strlen(js), "json: %s", js);
}

#ifdef QR_TEST_X11
/* Show a symbol in a window at a known place, then capture it. */
static void test_x11(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        CHECK(0, "no X display");
        return;
    }
    int scr = DefaultScreen(d);
    Visual *vis = DefaultVisual(d, scr);
    int depth = DefaultDepth(d, scr);
    const Symbol *s = &syms[4]; /* version 3 */
    QrImage im = render(s, 4);
    int wx = 120, wy = 70;
    XSetWindowAttributes swa;
    swa.override_redirect = True; /* no window manager placement */
    swa.background_pixel = WhitePixel(d, scr);
    Window win = XCreateWindow(d, RootWindow(d, scr), wx, wy, (unsigned)im.w, (unsigned)im.h, 0, depth, InputOutput,
                               vis, CWOverrideRedirect | CWBackPixel, &swa);
    XSelectInput(d, win, ExposureMask);
    XMapRaised(d, win);
    XImage *xi = XCreateImage(d, vis, (unsigned)depth, ZPixmap, 0, NULL, (unsigned)im.w, (unsigned)im.h, 32, 0);
    xi->data = malloc((size_t)xi->bytes_per_line * im.h);
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            unsigned g = im.gray[y * im.w + x];
            /* a tinted colour, so the capture's luma path matters */
            unsigned long r = g < 128 ? 30 : 255, gg = g < 128 ? 20 : 250, b = g < 128 ? 90 : 230;
            unsigned long px = (r * (vis->red_mask / 255)) & vis->red_mask;
            px |= (gg * (vis->green_mask / 255)) & vis->green_mask;
            px |= (b * (vis->blue_mask / 255)) & vis->blue_mask;
            XPutPixel(xi, x, y, px);
        }
    GC gc = XCreateGC(d, win, 0, NULL);
    XEvent ev;
    XWindowEvent(d, win, ExposureMask, &ev);
    XPutImage(d, win, gc, xi, 0, 0, 0, 0, (unsigned)im.w, (unsigned)im.h);
    XSync(d, False);

    char err[160];
    QrImage cap;
    SqqrResult res;
    QrRect r = {wx - 10, wy - 10, im.w + 20, im.h + 20};
    CHECK(qr_capture(r, &cap, err, sizeof err) == 0, "capture region: %s", err);
    if (cap.gray) {
        CHECK(cap.w == r.w && cap.h == r.h, "region size %dx%d", cap.w, cap.h);
        int rc = decode(&cap, &res);
        CHECK(rc == SQQR_OK && matches(s, &res), "region decode rc=%d", rc);
        if (rc == SQQR_OK)
            CHECK(res.corners[0] > 10 + 4 * 4 - 3 && res.corners[0] < 10 + 4 * 4 + 3, "corner x %.1f", res.corners[0]);
        qr_image_free(&cap);
    }
    QrRect full = {0, 0, 0, 0};
    CHECK(qr_capture(full, &cap, err, sizeof err) == 0, "capture screen: %s", err);
    if (cap.gray) {
        CHECK(cap.w == DisplayWidth(d, scr) && cap.h == DisplayHeight(d, scr), "screen size %dx%d", cap.w, cap.h);
        int rc = decode(&cap, &res);
        CHECK(rc == SQQR_OK && matches(s, &res), "screen decode rc=%d", rc);
        qr_image_free(&cap);
    }
    QrRect clipped = {DisplayWidth(d, scr) - 50, DisplayHeight(d, scr) - 50, 400, 400};
    CHECK(qr_capture(clipped, &cap, err, sizeof err) == 0 && cap.w == 50 && cap.h == 50, "clip: %dx%d", cap.w,
          cap.h);
    qr_image_free(&cap);
    QrRect outside = {DisplayWidth(d, scr) + 10, 0, 10, 10};
    CHECK(qr_capture(outside, &cap, err, sizeof err) < 0, "outside rect should fail");

    XDestroyImage(xi);
    XFreeGC(d, gc);
    XDestroyWindow(d, win);
    XCloseDisplay(d);
    free(im.gray);
}
#endif

/* --same A B: both images load and have identical pixels (test_png.py). */
static int same(const char *a, const char *b) {
    QrImage x, y;
    char err[160] = "";
    if (qr_image_load(a, &x, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", a, err);
        return 1;
    }
    if (qr_image_load(b, &y, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", b, err);
        qr_image_free(&x);
        return 1;
    }
    int bad = x.w != y.w || x.h != y.h;
    for (int i = 0; !bad && i < x.w * x.h; i++)
        if (x.gray[i] != y.gray[i]) {
            fprintf(stderr, "%s: pixel %d,%d is %d, want %d\n", a, i % x.w, i / x.w, x.gray[i], y.gray[i]);
            bad = 1;
        }
    if (x.w != y.w || x.h != y.h) fprintf(stderr, "%s: size %dx%d, want %dx%d\n", a, x.w, x.h, y.w, y.h);
    qr_image_free(&x);
    qr_image_free(&y);
    return bad;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--same")) return same(argv[2], argv[3]);
    int x11 = argc > 1 && !strcmp(argv[1], "--x11");
    if (argc != 3 + x11) {
        fprintf(stderr, "usage: %s [--x11] symbols.txt build-dir\n", argv[0]);
        return 2;
    }
    if (load_fixture(argv[1 + x11]) < 12) {
        fprintf(stderr, "cannot read fixtures %s\n", argv[1 + x11]);
        return 2;
    }
    if (x11) {
#ifdef QR_TEST_X11
        test_x11();
        printf("squirrelqr desktop X11 capture: %s\n", failures ? "FAILED" : "ok");
#else
        fprintf(stderr, "built without X11\n");
        return 2;
#endif
    } else {
        test_formats(argv[2 + x11]);
        test_text();
        printf("squirrelqr desktop: %s\n", failures ? "FAILED" : "ok");
    }
    return failures ? 1 : 0;
}

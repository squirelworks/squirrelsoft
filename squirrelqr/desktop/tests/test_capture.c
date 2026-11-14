/* Screen-capture path without a real display: synthetic frames via
 * QR_CAPTURE_FRAME, then sqqr decode. Usage: test_capture BUILD_DIR */
#include "qrcapture.h"
#include "qrdecode.h"
#include "qrimage.h"
#include "qrmake.h"
#include "sqqr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
static int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value);
}
static int unsetenv(const char *name) { return _putenv_s(name, ""); }
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

static int write_synth(const char *path, const char *text) {
    QrSymbol sym;
    char err[160];
    if (qr_make(text, strlen(text), SQQR_EC_M, &sym, err, sizeof err) != 0) {
        fprintf(stderr, "encode: %s\n", err);
        return -1;
    }
    QrImage im;
    if (qr_symbol_image(&sym, 4, 4, &im) != 0) return -1;
    int rc = qr_image_save_pgm(path, &im);
    qr_image_free(&im);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s build-dir\n", argv[0]);
        return 2;
    }
    const char *build = argv[1];
    char pgm[512];
    snprintf(pgm, sizeof pgm, "%s/capture_synth.pgm", build);

    const char *be = qr_capture_backend();
    CHECK(be && (!strcmp(be, "x11") || !strcmp(be, "portal") || !strcmp(be, "gdi-dxgi") ||
                 !strcmp(be, "sckit") || !strcmp(be, "none")),
          "unexpected backend %s", be ? be : "(null)");

    CHECK(write_synth(pgm, "squirrel-capture-synth") == 0, "write synth pgm");

    setenv("QR_CAPTURE_FRAME", pgm, 1);
    QrImage img;
    char err[200];
    QrRect full = {0, 0, 0, 0};
    CHECK(qr_capture(full, &img, err, sizeof err) == 0, "capture synth: %s", err);
    SqqrResult res;
    CHECK(qr_decode_gray(&img, &res) == SQQR_OK, "decode synth");
    CHECK(res.len == (int)strlen("squirrel-capture-synth") &&
              !memcmp(res.data, "squirrel-capture-synth", (size_t)res.len),
          "payload mismatch");
    qr_image_free(&img);

    QrRect sel;
    CHECK(qr_select_rect(&sel, err, sizeof err) == 0, "select synth: %s", err);
    CHECK(sel.w >= 8 && sel.h >= 8, "select size %dx%d", sel.w, sel.h);
    CHECK(qr_capture(sel, &img, err, sizeof err) == 0, "capture after select: %s", err);
    CHECK(qr_decode_gray(&img, &res) == SQQR_OK, "decode after select");
    qr_image_free(&img);

    QrRect crop = {4, 4, sel.w > 16 ? sel.w - 8 : sel.w, sel.h > 16 ? sel.h - 8 : sel.h};
    CHECK(qr_capture(crop, &img, err, sizeof err) == 0, "capture crop: %s", err);
    CHECK(img.w == crop.w && img.h == crop.h, "crop size %dx%d", img.w, img.h);
    qr_image_free(&img);

    unsetenv("QR_CAPTURE_FRAME");

    printf("squirrelqr capture: backend=%s %s\n", qr_capture_backend(), failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

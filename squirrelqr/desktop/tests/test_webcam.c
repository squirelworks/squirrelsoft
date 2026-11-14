/* Webcam path without a real camera: synthetic frames via @FILE and
 * QR_WEBCAM_FRAME, then sqqr decode. Usage: test_webcam BUILD_DIR */
#include "qrdecode.h"
#include "qrimage.h"
#include "qrmake.h"
#include "qrwebcam.h"
#include "sqqr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    char pgm[512], at[520], blank[512];
    snprintf(pgm, sizeof pgm, "%s/webcam_synth.pgm", build);
    snprintf(blank, sizeof blank, "%s/webcam_blank.pgm", build);
    snprintf(at, sizeof at, "@%s", pgm);

    CHECK(!strcmp(qr_webcam_backend(), "v4l2") || !strcmp(qr_webcam_backend(), "none") ||
              !strcmp(qr_webcam_backend(), "mf-stub") || !strcmp(qr_webcam_backend(), "avf-stub"),
          "backend %s", qr_webcam_backend());

    CHECK(write_synth(pgm, "squirrel-webcam-synth") == 0, "write synth pgm");

    QrImage img;
    char err[200];
    CHECK(qr_webcam_grab(at, &img, err, sizeof err) == 0, "grab @file: %s", err);
    SqqrResult res;
    CHECK(qr_decode_gray(&img, &res) == SQQR_OK, "decode synth");
    CHECK(res.len == (int)strlen("squirrel-webcam-synth") &&
              !memcmp(res.data, "squirrel-webcam-synth", (size_t)res.len),
          "payload mismatch");
    qr_image_free(&img);

    /* QR_WEBCAM_FRAME with a default device path. */
    setenv("QR_WEBCAM_FRAME", pgm, 1);
    CHECK(qr_webcam_grab(NULL, &img, err, sizeof err) == 0, "grab via env: %s", err);
    CHECK(qr_decode_gray(&img, &res) == SQQR_OK && res.len == (int)strlen("squirrel-webcam-synth"),
          "decode via env");
    qr_image_free(&img);
    unsetenv("QR_WEBCAM_FRAME");

    /* Blank frame: grab ok, decode not found. */
    FILE *f = fopen(blank, "wb");
    CHECK(f != NULL, "open blank");
    if (f) {
        fprintf(f, "P5\n64 64\n255\n");
        for (int i = 0; i < 64 * 64; i++) fputc(200, f);
        fclose(f);
    }
    snprintf(at, sizeof at, "@%s", blank);
    CHECK(qr_webcam_grab(at, &img, err, sizeof err) == 0, "grab blank: %s", err);
    CHECK(qr_decode_gray(&img, &res) != SQQR_OK, "blank should not decode");
    qr_image_free(&img);

    CHECK(qr_webcam_grab("@", &img, err, sizeof err) < 0, "bare @ should fail");
    CHECK(qr_webcam_list(stdout) >= 0, "list devices");

    printf("squirrelqr webcam: backend=%s %s\n", qr_webcam_backend(), failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

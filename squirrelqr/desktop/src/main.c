/* SquirrelQR desktop: decode QR codes from image files or from a rectangle of
 * the screen, with Squirrel's own decoder (libs/sqqr). */
#include "qrcapture.h"
#include "qrimage.h"
#include "qrtext.h"
#include "sqqr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef QR_VERSION
#define QR_VERSION "dev"
#endif

static void usage(FILE *f) {
    fprintf(f,
            "SquirrelQR desktop %s: read QR codes with Squirrel's own decoder (sqqr).\n"
            "\n"
            "usage: squirrelqr [options] IMAGE...      decode image files (PNG, PGM, PPM, PBM, BMP)\n"
            "       squirrelqr [options] --select      drag a rectangle on the screen, then decode it\n"
            "       squirrelqr [options] --screen      decode the whole screen\n"
            "       squirrelqr [options] --region X,Y,W,H\n"
            "\n"
            "options:\n"
            "  --json        print one JSON object per result (text, charset, version, corners)\n"
            "  --raw         write the payload bytes unchanged instead of UTF-8 text\n"
            "  --wait SECS   keep capturing the screen rectangle until a code shows up\n"
            "  --save FILE   also save each screen capture as a PGM image\n"
            "  -v            print the symbol version and error correction to stderr\n"
            "\n"
            "Screen capture back end: %s. Codes on screen need about 3 pixels per module.\n"
            "Exit status: 0 every input decoded, 1 a code was not found, 2 usage or I/O error.\n",
            QR_VERSION, qr_capture_backend());
}

typedef struct {
    int json, raw, verbose;
    double wait;
    const char *save;
} Opts;

/* Try the image as is, then 2x larger: small on-screen codes at 2 px/module
 * decode more reliably once each module covers several pixels. */
static int decode_gray(const QrImage *img, SqqrResult *res) {
    size_t need = SQQR_IMAGE_WORK(img->w, img->h);
    void *work = malloc(need);
    if (!work) return SQQR_ERR_SIZE;
    int rc = sqqr_decode_image(img->gray, img->w, img->h, img->w, work, need, res);
    free(work);
    if (rc == SQQR_OK || (long)img->w * img->h > 1200L * 1200L) return rc;
    int w2 = img->w * 2, h2 = img->h * 2;
    uint8_t *big = malloc((size_t)w2 * h2);
    need = SQQR_IMAGE_WORK(w2, h2);
    work = malloc(need);
    if (big && work) {
        for (int y = 0; y < h2; y++)
            for (int x = 0; x < w2; x++) big[(size_t)y * w2 + x] = img->gray[(size_t)(y / 2) * img->w + x / 2];
        int rc2 = sqqr_decode_image(big, w2, h2, w2, work, need, res);
        if (rc2 == SQQR_OK) {
            for (int i = 0; i < 8; i++) res->corners[i] /= 2;
            rc = rc2;
        }
    }
    free(big);
    free(work);
    return rc;
}

static void report(const Opts *o, const char *source, const SqqrResult *res, int ox, int oy) {
    if (o->verbose)
        fprintf(stderr, "%s: version %d-%c, mask %d, %d codewords corrected\n", source, res->version,
                "LMQH"[res->ec_level], res->mask, res->corrected);
    if (o->raw) {
        fwrite(res->data, 1, res->len, stdout);
        putchar('\n');
        return;
    }
    size_t cap = 4 * res->len + 1;
    char *text = malloc(cap);
    if (!text) return;
    const char *cs = qr_payload_text(res->data, res->len, res->eci, res->has_kanji, text, cap);
    if (!o->json) {
        fputs(text, stdout);
        putchar('\n');
        free(text);
        return;
    }
    size_t jn = qr_json_string(text, NULL, 0) + 1, sn = qr_json_string(source, NULL, 0) + 1;
    char *jt = malloc(jn), *js = malloc(sn);
    if (jt && js) {
        qr_json_string(text, jt, jn);
        qr_json_string(source, js, sn);
        printf("{\"source\":%s,\"text\":%s,\"charset\":\"%s\",\"version\":%d,\"ec\":\"%c\",\"eci\":%d,"
               "\"corners\":[",
               js, jt, cs, res->version, "LMQH"[res->ec_level], res->eci);
        for (int i = 0; i < 8; i++) printf("%s%.1f", i ? "," : "", res->corners[i] + (i % 2 ? oy : ox));
        printf("]}\n");
    }
    free(jt);
    free(js);
    free(text);
}

static int do_file(const Opts *o, const char *path, SqqrResult *res) {
    QrImage img;
    char err[160];
    if (qr_image_load(path, &img, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 2;
    }
    int rc = decode_gray(&img, res);
    qr_image_free(&img);
    if (rc != SQQR_OK) {
        fprintf(stderr, "%s: %s\n", path, sqqr_strerror(rc));
        return 1;
    }
    report(o, path, res, 0, 0);
    return 0;
}

static void nap(double s) {
    struct timespec ts = {(time_t)s, (long)((s - (double)(time_t)s) * 1e9)};
    nanosleep(&ts, NULL);
}

static int do_screen(const Opts *o, QrRect r, SqqrResult *res) {
    char err[160];
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        QrImage img;
        if (qr_capture(r, &img, err, sizeof err)) {
            fprintf(stderr, "screen: %s\n", err);
            return 2;
        }
        if (o->save && qr_image_save_pgm(o->save, &img)) fprintf(stderr, "%s: cannot write\n", o->save);
        int rc = decode_gray(&img, res);
        qr_image_free(&img);
        if (rc == SQQR_OK) {
            report(o, "screen", res, r.w > 0 && r.h > 0 && r.x > 0 ? r.x : 0, /* qr_capture clips to the screen */
                   r.w > 0 && r.h > 0 && r.y > 0 ? r.y : 0);
            return 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &t);
        double elapsed = (double)(t.tv_sec - t0.tv_sec) + (double)(t.tv_nsec - t0.tv_nsec) / 1e9;
        if (elapsed >= o->wait) {
            fprintf(stderr, "screen: %s\n", sqqr_strerror(rc));
            if (getenv("WAYLAND_DISPLAY"))
                fprintf(stderr, "note: under Wayland the X11 capture only sees XWayland windows;\n"
                                "      take a screenshot with the desktop's tool and pass the PNG file\n");
            return 1;
        }
        nap(0.3);
    }
}

int main(int argc, char **argv) {
    Opts o = {0, 0, 0, 0.0, NULL};
    int screen = 0, select = 0, nfiles = 0;
    QrRect r = {0, 0, 0, 0};
    char **files = argv + 1; /* file names are compacted in place; the write index never passes the read one */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--version")) {
            printf("squirrelqr %s (capture: %s)\n", QR_VERSION, qr_capture_backend());
            return 0;
        } else if (!strcmp(a, "--json")) {
            o.json = 1;
        } else if (!strcmp(a, "--raw")) {
            o.raw = 1;
        } else if (!strcmp(a, "-v")) {
            o.verbose = 1;
        } else if (!strcmp(a, "--screen")) {
            screen = 1;
        } else if (!strcmp(a, "--select")) {
            select = 1;
        } else if (!strcmp(a, "--region") && i + 1 < argc) {
            char tail;
            if (sscanf(argv[++i], "%d,%d,%d,%d%c", &r.x, &r.y, &r.w, &r.h, &tail) != 4 || r.w < 1 || r.h < 1) {
                fprintf(stderr, "--region wants X,Y,W,H\n");
                return 2;
            }
            screen = 1;
        } else if (!strcmp(a, "--wait") && i + 1 < argc) {
            o.wait = atof(argv[++i]);
        } else if (!strcmp(a, "--save") && i + 1 < argc) {
            o.save = argv[++i];
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "unknown option %s\n\n", a);
            usage(stderr);
            return 2;
        } else {
            files[nfiles++] = argv[i];
        }
    }
    if (o.json && o.raw) {
        fprintf(stderr, "--json and --raw don't mix\n");
        return 2;
    }
    if ((screen || select) == (nfiles > 0) || (screen && select)) {
        usage(stderr);
        return 2;
    }
    SqqrResult *res = malloc(sizeof *res);
    if (!res) return 2;
    int status = 0;
    if (select) {
        char err[160];
        int s = qr_select_rect(&r, err, sizeof err);
        if (s == 1) {
            fprintf(stderr, "cancelled\n");
            status = 1;
        } else if (s < 0) {
            fprintf(stderr, "select: %s\n", err);
            status = 2;
        } else {
            status = do_screen(&o, r, res);
        }
    } else if (screen) {
        status = do_screen(&o, r, res);
    } else {
        for (int i = 0; i < nfiles; i++) {
            int s = do_file(&o, files[i], res);
            if (s > status) status = s;
        }
    }
    free(res);
    return status;
}

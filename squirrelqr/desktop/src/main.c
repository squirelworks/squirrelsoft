/* SquirrelQR desktop: decode QR codes from image files or from a rectangle of
 * the screen, with Squirrel's own decoder (libs/sqqr). */
#include "qrcapture.h"
#include "qrdecode.h"
#include "qrgui_run.h"
#include "qrmake.h"
#include "qrimage.h"
#include "qrtext.h"
#include "sqqr.h"
#include "sqqr_content.h"

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
            "       squirrelqr --gui [IMAGE]           a small window: select, screen, open, copy, make\n"
            "       squirrelqr --encode TEXT [--out FILE.png|.svg|.pgm]   make a QR code (- reads stdin)\n"
            "       squirrelqr --wifi SSID [--password P] [--security WPA|WEP|SAE|nopass] [--hidden] [--out ...]\n"
            "\n"
            "options:\n"
            "  --json        print one JSON object per result (text, charset, version, corners)\n"
            "  --raw         write the payload bytes unchanged instead of UTF-8 text\n"
            "  --wait SECS   keep capturing the screen rectangle until a code shows up\n"
            "  --save FILE   also save each screen capture as a PGM image\n"
            "  --gui-frames N  close the window after N frames (for CI)\n"
            "  -v            print the symbol version and error correction to stderr\n"
            "  --details     after the text, what it is (Wi-Fi, contact, link, ...) and its fields\n"
            "\n"
            "making codes:\n"
            "  --level L|M|Q|H  error correction (default M)\n"
            "  --out FILE    write .png, .svg or .pgm; without it the code is drawn in the terminal\n"
            "  --scale N     pixels per module for PNG/PGM (default 8)\n"
            "  --border N    quiet zone in modules (default 4)\n"
            "  --invert      terminal drawing for a light background\n"
            "\n"
            "Screen capture back end: %s. Codes on screen need about 3 pixels per module.\n"
            "Exit status: 0 every input decoded, 1 a code was not found, 2 usage or I/O error.\n",
            QR_VERSION, qr_capture_backend());
}

typedef struct {
    int json, raw, verbose, details;
    double wait;
    const char *save;
} Opts;

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
        if (o->details) {
            static SqqrContent ct;
            sqqr_content_parse(text, &ct);
            printf("  [%s]\n", sqqr_content_type_name(ct.type));
            for (int i = 0; i < ct.nfields; i++) printf("  %s: %s\n", ct.field[i].label, ct.field[i].value);
        }
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
        static SqqrContent ct;
        sqqr_content_parse(text, &ct);
        printf("],\"type\":\"%s\",\"fields\":[", sqqr_content_type_id(ct.type));
        for (int i = 0; i < ct.nfields; i++) {
            static char lab[64], val[6 * sizeof ct.buf + 3];
            qr_json_string(ct.field[i].label, lab, sizeof lab);
            qr_json_string(ct.field[i].value, val, sizeof val);
            printf("%s[%s,%s]", i ? "," : "", lab, val);
        }
        printf("]}\n");
    }
    free(jt);
    free(js);
    free(text);
}

static int do_encode(const char *encode, const char *wifi, const char *password, const char *security,
                     int hidden, int level, const char *out, int scale, int border, int invert) {
    static char text[8192];
    size_t len;
    if (wifi) {
        if (security && strcmp(security, "WPA") && strcmp(security, "WEP") && strcmp(security, "SAE") &&
            strcmp(security, "nopass")) {
            fprintf(stderr, "--security wants WPA, WEP, SAE or nopass\n");
            return 2;
        }
        len = sqqr_content_wifi(wifi, password, security, hidden, text, sizeof text);
        if (len >= sizeof text) {
            fprintf(stderr, "Wi-Fi details too long\n");
            return 2;
        }
    } else if (!strcmp(encode, "-")) {
        len = fread(text, 1, sizeof text - 1, stdin);
        if (len == sizeof text - 1 && fgetc(stdin) != EOF) {
            fprintf(stderr, "stdin: more than %zu bytes\n", sizeof text - 1);
            return 2;
        }
        while (len && (text[len - 1] == '\n' || text[len - 1] == '\r')) len--; /* echo adds one */
    } else {
        len = strlen(encode);
        if (len >= sizeof text) {
            fprintf(stderr, "text too long\n");
            return 2;
        }
        memcpy(text, encode, len);
    }
    text[len] = 0;
    static QrSymbol sym;
    char err[200];
    if (qr_make(text, len, (SqqrEcLevel)level, &sym, err, sizeof err) != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    fprintf(stderr, "version %d-%c, %d x %d modules, mask %d\n", sym.info.version, "LMQH"[level],
            sym.info.size, sym.info.size, sym.info.mask);
    if (!out) {
        qr_print_terminal(stdout, &sym, border, invert);
        return 0;
    }
    if (qr_save_symbol(out, &sym, scale, border, err, sizeof err) != 0) {
        fprintf(stderr, "%s\n", err);
        return 2;
    }
    fprintf(stderr, "wrote %s\n", out);
    return 0;
}

static int do_file(const Opts *o, const char *path, SqqrResult *res) {
    QrImage img;
    char err[160];
    if (qr_image_load(path, &img, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 2;
    }
    int rc = qr_decode_gray(&img, res);
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
        int rc = qr_decode_gray(&img, res);
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
    Opts o = {0, 0, 0, 0, 0.0, NULL};
    const char *encode = NULL, *wifi = NULL, *password = NULL, *security = NULL, *out = NULL;
    int hidden = 0, level = SQQR_EC_M, scale = 8, border = 4, invert = 0;
    int screen = 0, select = 0, nfiles = 0, gui = 0, gui_frames = 0;
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
        } else if (!strcmp(a, "--details")) {
            o.details = 1;
        } else if (!strcmp(a, "--encode") && i + 1 < argc) {
            encode = argv[++i];
        } else if (!strcmp(a, "--wifi") && i + 1 < argc) {
            wifi = argv[++i];
        } else if (!strcmp(a, "--password") && i + 1 < argc) {
            password = argv[++i];
        } else if (!strcmp(a, "--security") && i + 1 < argc) {
            security = argv[++i];
        } else if (!strcmp(a, "--hidden")) {
            hidden = 1;
        } else if (!strcmp(a, "--level") && i + 1 < argc) {
            level = qr_level_parse(argv[++i]);
            if (level < 0) {
                fprintf(stderr, "--level wants L, M, Q or H\n");
                return 2;
            }
        } else if (!strcmp(a, "--out") && i + 1 < argc) {
            out = argv[++i];
        } else if (!strcmp(a, "--scale") && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (!strcmp(a, "--border") && i + 1 < argc) {
            border = atoi(argv[++i]);
        } else if (!strcmp(a, "--invert")) {
            invert = 1;
        } else if (!strcmp(a, "--gui")) {
            gui = 1;
        } else if (!strcmp(a, "--gui-frames") && i + 1 < argc) {
            gui = 1;
            gui_frames = atoi(argv[++i]);
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
    if (encode || wifi) {
        if ((encode && wifi) || screen || select || gui || nfiles) {
            usage(stderr);
            return 2;
        }
        if (scale < 1 || scale > 64 || border < 0 || border > 40) {
            fprintf(stderr, "--scale wants 1..64 and --border 0..40\n");
            return 2;
        }
        return do_encode(encode, wifi, password, security, hidden, level, out, scale, border, invert);
    }
    if (gui) {
        if (screen || select || nfiles > 1) {
            usage(stderr);
            return 2;
        }
        return qr_gui_run(nfiles ? files[0] : NULL, gui_frames);
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

/* sqqr_decode: find and decode a QR symbol in a binary PGM (P5, 8-bit) image.
 * Usage: sqqr_decode image.pgm      (prints the payload, exit 0)
 * Convert other formats first, e.g.  convert photo.jpg -colorspace gray photo.pgm */
#include "sqqr.h"

#include <stdio.h>
#include <stdlib.h>

static int token(FILE *f) { /* next header integer, skipping whitespace and comments */
    int c, v = 0, any = 0;
    while ((c = fgetc(f)) != EOF) {
        if (c == '#') {
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            any = 1;
            if (v > 1 << 20) return -1;
        } else if (any) {
            return v;
        }
    }
    return any ? v : -1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s image.pgm\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    int w, h, maxv;
    if (fgetc(f) != 'P' || fgetc(f) != '5' || (w = token(f)) < 1 || (h = token(f)) < 1 ||
        (maxv = token(f)) != 255) {
        fprintf(stderr, "%s: not an 8-bit binary PGM (P5)\n", argv[1]);
        fclose(f);
        return 2;
    }
    size_t n = (size_t)w * (size_t)h;
    unsigned char *img = malloc(n), *work = malloc(SQQR_IMAGE_WORK(w, h));
    SqqrResult *res = malloc(sizeof *res);
    if (!img || !work || !res || fread(img, 1, n, f) != n) {
        fprintf(stderr, "%s: short read or out of memory\n", argv[1]);
        fclose(f);
        return 2;
    }
    fclose(f);
    int rc = sqqr_decode_image(img, w, h, w, work, SQQR_IMAGE_WORK(w, h), res);
    if (rc != SQQR_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], sqqr_strerror(rc));
        return 1;
    }
    fprintf(stderr, "version %d-%c, mask %d, %d codewords corrected\n", res->version, "LMQH"[res->ec_level],
            res->mask, res->corrected);
    fwrite(res->data, 1, res->len, stdout);
    putchar('\n');
    free(img);
    free(work);
    free(res);
    return 0;
}

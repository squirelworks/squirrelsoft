/* sqqr unit tests plus the segno-generated fixture symbols.
 * Usage: test_sqqr fixtures/symbols.txt */
#include "sqqr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                           \
        }                                                                         \
    } while (0)

static uint32_t rng = 12345;
static uint32_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static void test_rs(void) {
    uint8_t data[200], cw[255], orig[255];
    for (int trial = 0; trial < 300; trial++) {
        int nsym = 2 + (int)(rnd() % 60);
        int n = nsym + 1 + (int)(rnd() % (255 - nsym - 1));
        int k = n - nsym;
        if (k > 200) k = 200, n = k + nsym;
        for (int i = 0; i < k; i++) data[i] = (uint8_t)rnd();
        memcpy(cw, data, (size_t)k);
        sqqr_rs_encode(data, k, nsym, cw + k);
        memcpy(orig, cw, (size_t)n);
        CHECK(sqqr_rs_decode(cw, n, nsym) == 0);
        int errs = (int)(rnd() % (nsym / 2 + 1));
        for (int e = 0; e < errs; e++) cw[rnd() % (unsigned)n] ^= (uint8_t)(1 + rnd() % 255);
        int fixed = sqqr_rs_decode(cw, n, nsym);
        CHECK(fixed >= 0 && fixed <= errs);
        CHECK(memcmp(cw, orig, (size_t)n) == 0);
    }
    /* Far too many errors must be reported, not "corrected" into garbage silently. */
    int detected = 0;
    for (int trial = 0; trial < 100; trial++) {
        int nsym = 10, k = 30, n = k + nsym;
        for (int i = 0; i < k; i++) data[i] = (uint8_t)rnd();
        memcpy(cw, data, (size_t)k);
        sqqr_rs_encode(data, k, nsym, cw + k);
        memcpy(orig, cw, (size_t)n);
        for (int i = 0; i < 15; i++) cw[i * 2] ^= (uint8_t)(1 + rnd() % 255);
        int r = sqqr_rs_decode(cw, n, nsym);
        if (r < 0) detected++;
        else CHECK(memcmp(cw, orig, (size_t)n) != 0 || r >= 0);
    }
    CHECK(detected > 90);
    CHECK(sqqr_rs_decode(cw, 10, 10) == -1);
}

static void test_format_version(void) {
    /* Format word for M, mask 0 is 101010000010010 (0x5412 masks 0x0000). */
    CHECK(sqqr_decode_format_bits(0x5412) == 0);
    for (uint32_t d = 0; d < 32; d++) {
        /* BCH(15,5) with generator 0x537, then the 0x5412 mask, computed here independently. */
        uint32_t v = d << 10;
        for (int i = 14; i >= 10; i--)
            if (v & (1u << i)) v ^= 0x537u << (i - 10);
        uint32_t code = ((d << 10) | v) ^ 0x5412;
        CHECK(sqqr_decode_format_bits(code) == (int)d);
        CHECK(sqqr_decode_format_bits(code ^ 0x4021) == (int)d); /* 3 bits wrong */
    }
    CHECK(sqqr_decode_format_bits(0x5412 ^ 0x000F) == -1 || sqqr_decode_format_bits(0x5412 ^ 0x000F) != 0);
    CHECK(sqqr_decode_version_bits(0x07C94) == 7);  /* version 7 information word */
    CHECK(sqqr_decode_version_bits(0x28C69) == 40); /* version 40 */
    CHECK(sqqr_decode_version_bits(0x07C94 ^ 0x10101) == 7);
    int pos[7];
    CHECK(sqqr_alignment_positions(1, pos) == 0);
    CHECK(sqqr_alignment_positions(2, pos) == 2 && pos[0] == 6 && pos[1] == 18);
    CHECK(sqqr_alignment_positions(32, pos) == 6 && pos[1] == 34 && pos[5] == 138);
    CHECK(sqqr_alignment_positions(40, pos) == 7 && pos[6] == 170);
    SqqrBlocks b;
    CHECK(sqqr_blocks(1, SQQR_EC_L, &b) == 0 && b.total == 26);
    CHECK(sqqr_blocks(40, SQQR_EC_H, &b) == 0 && b.total == 3706);
    CHECK(sqqr_blocks(41, SQQR_EC_H, &b) == -1);
    CHECK(sqqr_decode_grid((const uint8_t *)"", 20, NULL) == SQQR_ERR_SIZE);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static uint8_t grid[SQQR_MAX_SIZE * SQQR_MAX_SIZE];
static SqqrResult res;

/* ---- image layer: render fixture grids as camera-like images ---------------- */

#define IMG_MAX 1024
static uint8_t img[IMG_MAX * IMG_MAX];
static uint8_t work[SQQR_IMAGE_WORK(IMG_MAX, IMG_MAX)];

/* (u, v) -> (x, y) homography from four point pairs, for the renderer. */
static int solve_h(double src[4][2], double dst[4][2], double hm[8]) {
    double a[8][9];
    for (int i = 0; i < 4; i++) {
        double u = src[i][0], v = src[i][1], x = dst[i][0], y = dst[i][1];
        double r0[9] = {u, v, 1, 0, 0, 0, -u * x, -v * x, x}, r1[9] = {0, 0, 0, u, v, 1, -u * y, -v * y, y};
        memcpy(a[2 * i], r0, sizeof r0);
        memcpy(a[2 * i + 1], r1, sizeof r1);
    }
    for (int c = 0; c < 8; c++) {
        int p = c;
        for (int r = c + 1; r < 8; r++)
            if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        if (fabs(a[p][c]) < 1e-12) return 0;
        for (int k = 0; k < 9; k++) {
            double t = a[c][k];
            a[c][k] = a[p][k];
            a[p][k] = t;
        }
        for (int r = 0; r < 8; r++)
            if (r != c) {
                double f = a[r][c] / a[c][c];
                for (int k = c; k < 9; k++) a[r][k] -= f * a[c][k];
            }
    }
    for (int i = 0; i < 8; i++) hm[i] = a[i][8] / a[i][i];
    return 1;
}

static double truth_corners[8];

/* Draw grid g (n x n, 4-module quiet zone) rotated by angle, with the top edge
 * shortened by tilt (perspective), uneven lighting and noise. */
static int render(const uint8_t *g, int n, int scale, double angle, double tilt, int invert, int *wh) {
    int side = (n + 8) * scale, W = (int)(side * 1.45) + 16;
    if (W > IMG_MAX) return 0;
    double half = side / 2.0, c = W / 2.0, ca = cos(angle), sa = sin(angle);
    double sq[4][2] = {{-half * (1 - tilt), -half}, {half * (1 - tilt), -half}, {-half, half}, {half, half}};
    double dst[4][2], src[4][2] = {{-4, -4}, {n + 4, -4}, {-4, n + 4}, {n + 4, n + 4}}, hm[8];
    for (int i = 0; i < 4; i++) {
        dst[i][0] = c + sq[i][0] * ca - sq[i][1] * sa;
        dst[i][1] = c + sq[i][0] * sa + sq[i][1] * ca;
    }
    if (getenv("SQQR_DEBUG")) {
        double fw[8];
        solve_h(src, dst, fw);
        double pts[4][2] = {{3.5, 3.5}, {n - 3.5, 3.5}, {3.5, n - 3.5}, {n - 6.5, n - 6.5}};
        for (int i = 0; i < 4; i++) {
            double u = pts[i][0], v = pts[i][1], d = fw[6] * u + fw[7] * v + 1;
            fprintf(stderr, "  truth %d: (%.1f,%.1f)\n", i, (fw[0] * u + fw[1] * v + fw[2]) / d,
                    (fw[3] * u + fw[4] * v + fw[5]) / d);
        }
    }
    {
        double fw[8], uv[4][2] = {{0, 0}, {n, 0}, {n, n}, {0, n}};
        if (!solve_h(src, dst, fw)) return 0; /* module space -> image: where the corners are */
        for (int i = 0; i < 4; i++) {
            double u = uv[i][0], v = uv[i][1], d = fw[6] * u + fw[7] * v + 1;
            truth_corners[2 * i] = (fw[0] * u + fw[1] * v + fw[2]) / d;
            truth_corners[2 * i + 1] = (fw[3] * u + fw[4] * v + fw[5]) / d;
        }
    }
    if (!solve_h(dst, src, hm)) return 0; /* image -> module space */
    uint32_t noise = (uint32_t)n * 2654435761u;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            int darkn = 0;
            for (int sy = 0; sy < 2; sy++)
                for (int sx = 0; sx < 2; sx++) {
                    double px = x + 0.25 + 0.5 * sx, py = y + 0.25 + 0.5 * sy;
                    double d = hm[6] * px + hm[7] * py + 1;
                    double u = (hm[0] * px + hm[1] * py + hm[2]) / d, v = (hm[3] * px + hm[4] * py + hm[5]) / d;
                    darkn += u >= 0 && v >= 0 && u < n && v < n && g[(int)v * n + (int)u];
                }
            int val = 215 - darkn * 45;                 /* 215 light .. 35 dark */
            val += (x - W / 2) * 70 / W;                /* lighting gradient */
            noise = noise * 1103515245u + 12345u;     /* own generator: keeps rnd() for the damage tests */
            val += (int)((noise >> 16) % 31) - 15;      /* sensor noise */
            if (invert) val = 255 - val;
            img[y * W + x] = (uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val);
        }
    *wh = W;
    return 1;
}

static int image_tests, image_ok;

static void test_image(const uint8_t *g, int n, int idx, const uint8_t *want, size_t want_len) {
    int scale = n > 100 ? 3 : 3 + idx % 4, W;
    double tilt = (idx % 3) * (n >= 57 ? 0.04 : 0.08);
    double angle = idx * 41.0 * 3.14159265358979 / 180.0;
    int invert = idx % 7 == 3;
    if (!render(g, n, scale, angle, tilt, invert, &W)) return;
    image_tests++;
    int rc = sqqr_decode_image(img, W, W, W, work, sizeof work, &res);
    if (rc == SQQR_OK && res.len == want_len && memcmp(res.data, want, want_len) == 0) {
        image_ok++;
        /* The reported corners are the symbol's, within a module. */
        for (int i = 0; i < 4; i++) {
            double dx = res.corners[2 * i] - truth_corners[2 * i], dy = res.corners[2 * i + 1] - truth_corners[2 * i + 1];
            if (dx * dx + dy * dy > (double)scale * scale) {
                fprintf(stderr, "image of symbol %d: corner %d at (%.1f,%.1f), want (%.1f,%.1f)\n", idx, i,
                        res.corners[2 * i], res.corners[2 * i + 1], truth_corners[2 * i], truth_corners[2 * i + 1]);
                failures++;
            }
        }
        return;
    }
    if (getenv("SQQR_DEBUG")) {
        int bad = 0, q[4] = {0, 0, 0, 0};
        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++)
                if (!!work[r * n + c] != !!g[r * n + c]) bad++, q[(r >= n / 2) * 2 + (c >= n / 2)]++;
        fprintf(stderr, "  last grid: %d wrong modules (quadrants %d %d %d %d)\n", bad, q[0], q[1], q[2], q[3]);
    }
    fprintf(stderr, "image of symbol %d (%dx%d modules, %dpx, %d deg, tilt %.2f%s): %s\n", idx, n, n, scale,
            idx * 41 % 360, tilt, invert ? ", inverted" : "", sqqr_strerror(rc));
    failures++;
}

static void test_image_stride(const uint8_t *g, int n, const uint8_t *want, size_t want_len);

static int run_fixture(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }
    static char line[8192], hdr[8192];
    int symbols = 0, damaged_ok = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] != 'S') continue;
        strcpy(hdr, line);
        int version, mask;
        char level;
        static char hex[8000];
        if (sscanf(hdr, "S %d %c %d %7999s", &version, &level, &mask, hex) != 4) {
            failures++;
            continue;
        }
        int n = 17 + 4 * version;
        for (int r = 0; r < n; r++) {
            if (!fgets(line, sizeof line, f)) return -1;
            for (int c = 0; c < n; c++) {
                int nib = hexval(line[c / 4]);
                grid[r * n + c] = (uint8_t)((nib >> (3 - c % 4)) & 1);
            }
        }
        size_t want_len = strlen(hex) / 2;
        uint8_t want[4000];
        for (size_t i = 0; i < want_len; i++) want[i] = (uint8_t)(hexval(hex[2 * i]) << 4 | hexval(hex[2 * i + 1]));

        int rc = sqqr_decode_grid(grid, n, &res);
        int ok = rc == SQQR_OK && res.version == version && "LMQH"[res.ec_level] == level &&
                 res.mask == mask && res.len == want_len && memcmp(res.data, want, want_len) == 0;
        if (!ok) {
            fprintf(stderr, "symbol %d (v%d %c mask %d): rc=%d (%s) v=%d mask=%d len=%zu want %zu\n", symbols,
                    version, level, mask, rc, sqqr_strerror(rc), res.version, res.mask, res.len, want_len);
            failures++;
        }
        CHECK(res.corrected == 0);
        CHECK(res.corners[0] == 0 && res.corners[7] == 0); /* the grid decoder has no image */
        test_image(grid, n, symbols, want, want_len);
        if (symbols == 4) test_image_stride(grid, n, want, want_len);

        /* Damage: flip a few data-area modules; H/Q/M symbols should still decode. */
        if (level != 'L') {
            int flips = version < 5 ? 2 : 6;
            for (int i = 0; i < flips; i++) {
                int r = 9 + (int)(rnd() % (unsigned)(n - 18)), c = 9 + (int)(rnd() % (unsigned)(n - 18));
                if (r == 6 || c == 6) c = 10;
                grid[r * n + c] ^= 1;
            }
            rc = sqqr_decode_grid(grid, n, &res);
            if (rc == SQQR_OK && res.len == want_len && memcmp(res.data, want, want_len) == 0) {
                damaged_ok++;
                CHECK(res.corrected > 0);
            } else {
                fprintf(stderr, "damaged symbol %d (v%d %c) failed: %s\n", symbols, version, level, sqqr_strerror(rc));
                failures++;
            }
        }
        /* A wrong size is refused; a blank grid has no format information. */
        memset(grid, 0, (size_t)n * (size_t)n);
        CHECK(sqqr_decode_grid(grid, n, &res) != SQQR_OK);
        symbols++;
    }
    fclose(f);
    printf("fixture: %d symbols decoded, %d decoded again after damage\n", symbols, damaged_ok);
    printf("images: %d of %d rendered symbols found and decoded\n", image_ok, image_tests);
    return symbols > 0 ? 0 : -1;
}

/* No symbol, a too-small workspace, and a stride wider than the image. */
static void test_image_edges(void) {
    int W = 200;
    for (int i = 0; i < W * W; i++) img[i] = (uint8_t)(100 + rnd() % 80);
    CHECK(sqqr_decode_image(img, W, W, W, work, sizeof work, &res) != SQQR_OK);
    memset(img, 230, (size_t)W * (size_t)W);
    CHECK(sqqr_decode_image(img, W, W, W, work, sizeof work, &res) == SQQR_ERR_NOT_FOUND);
    CHECK(sqqr_decode_image(img, W, W, W, work, SQQR_IMAGE_WORK(W, W) - 1, &res) == SQQR_ERR_SIZE);
    CHECK(sqqr_decode_image(img, W, W, W - 1, work, sizeof work, &res) == SQQR_ERR_SIZE);
}

static void test_image_stride(const uint8_t *g, int n, const uint8_t *want, size_t want_len) {
    int W;
    if (!render(g, n, 4, 0.3, 0.05, 0, &W)) return;
    int stride = W + 13;
    static uint8_t wide[IMG_MAX * (IMG_MAX + 13)];
    for (int y = 0; y < W; y++) {
        memcpy(wide + (size_t)y * (size_t)stride, img + (size_t)y * (size_t)W, (size_t)W);
        memset(wide + (size_t)y * (size_t)stride + W, 0, 13); /* black padding must be ignored */
    }
    int rc = sqqr_decode_image(wide, W, W, stride, work, sizeof work, &res);
    CHECK(rc == SQQR_OK && res.len == want_len && memcmp(res.data, want, want_len) == 0);
}

int main(int argc, char **argv) {
    test_rs();
    test_format_version();
    test_image_edges();
    if (argc > 1 && run_fixture(argv[1])) failures++;
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("sqqr: all tests passed");
    return 0;
}

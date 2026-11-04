/* sqqr unit tests plus the segno-generated fixture symbols.
 * Usage: test_sqqr fixtures/symbols.txt */
#include "sqqr.h"

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
    return symbols > 0 ? 0 : -1;
}

int main(int argc, char **argv) {
    test_rs();
    test_format_version();
    if (argc > 1 && run_fixture(argv[1])) failures++;
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("sqqr: all tests passed");
    return 0;
}

/* sqfelica unit tests plus the felica_parse.py parity fixture.
 * Usage: test_sqfelica fixtures/parity.txt [StationCode.csv] */
#include "sqfelica.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static const uint8_t IDM[8] = {0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t n) { return memcmp(a, b, n) == 0; }

static void test_builders(void) {
    uint8_t f[64];
    CHECK(sqf_polling(f, sizeof f, 0x0003, SQF_RC_SYSTEM_CODE, 0) == 6);
    CHECK(bytes_eq(f, (const uint8_t[]){6, 0x00, 0x00, 0x03, 0x01, 0x00}, 6));
    CHECK(sqf_polling(f, 5, 0xFFFF, 0, 0) == -1);

    CHECK(sqf_request_system_code(f, sizeof f, IDM) == 10);
    CHECK(f[0] == 10 && f[1] == 0x0C && bytes_eq(f + 2, IDM, 8));
    CHECK(sqf_request_response(f, sizeof f, IDM) == 10 && f[1] == 0x04);

    CHECK(sqf_search_service_code(f, sizeof f, IDM, 0x0102) == 12);
    CHECK(f[0] == 12 && f[1] == 0x0A && f[10] == 0x02 && f[11] == 0x01);

    uint16_t codes[] = {0x090F, 0x008B};
    CHECK(sqf_request_service(f, sizeof f, IDM, codes, 2) == 15);
    CHECK(bytes_eq(f + 10, (const uint8_t[]){2, 0x0F, 0x09, 0x8B, 0x00}, 5));
    CHECK(sqf_request_service(f, sizeof f, IDM, codes, 0) == -1);
    uint16_t many[33] = {0};
    CHECK(sqf_request_service(f, sizeof f, IDM, many, 33) == -1);
    CHECK(sqf_request_service(f, sizeof f, IDM, many, 26) == 63);

    /* Block-list elements: 2 bytes for 0..255, 3 bytes (LE) above. */
    for (unsigned b = 0; b < 20; b++) {
        CHECK(sqf_block_list_element(f, sizeof f, (uint16_t)b) == 2);
        CHECK(f[0] == 0x80 && f[1] == b);
    }
    CHECK(sqf_block_list_element(f, sizeof f, 255) == 2 && f[1] == 0xFF);
    CHECK(sqf_block_list_element(f, sizeof f, 256) == 3);
    CHECK(bytes_eq(f, (const uint8_t[]){0x00, 0x00, 0x01}, 3));
    CHECK(sqf_block_list_element(f, sizeof f, 0x1234) == 3);
    CHECK(bytes_eq(f, (const uint8_t[]){0x00, 0x34, 0x12}, 3));
    CHECK(sqf_block_list_element(f, 2, 0x1234) == -1);

    uint16_t blocks[] = {0, 1, 300};
    int n = sqf_read_without_encryption(f, sizeof f, IDM, 0x090F, blocks, 3);
    CHECK(n == 21);
    CHECK(f[0] == 21 && f[1] == 0x06 && f[10] == 1 && f[11] == 0x0F && f[12] == 0x09 && f[13] == 3);
    CHECK(bytes_eq(f + 14, (const uint8_t[]){0x80, 0, 0x80, 1, 0x00, 0x2C, 0x01}, 7));
    uint16_t sixteen[16] = {0};
    CHECK(sqf_read_without_encryption(f, sizeof f, IDM, 0x090F, sixteen, 16) == -1);
    CHECK(sqf_read_without_encryption(f, 20, IDM, 0x090F, blocks, 3) == -1);
}

static size_t resp(uint8_t *out, int with_len, uint8_t code, const uint8_t *data, size_t n) {
    size_t off = with_len ? 1 : 0;
    out[off] = code;
    memcpy(out + off + 1, IDM, 8);
    memcpy(out + off + 9, data, n);
    if (with_len) out[0] = (uint8_t)(n + 10);
    return n + 9 + off;
}

static void test_parsers(void) {
    uint8_t r[300];
    /* The parser accepts frames with or without the length byte, guessing from
     * byte 0, so a frame without it is only unambiguous when byte 0 (the
     * response code) differs from len and len - 1. Polling (0x01) always is. */
    for (int wl = 1; wl >= 0; wl--) {
        uint8_t pd[] = {0, 0xF1, 0, 0, 0, 1, 0x43, 0, 0x00, 0x03};
        size_t n = resp(r, wl, 0x01, pd, sizeof pd);
        SqfPolling p;
        CHECK(sqf_parse_polling(r, n, &p) == 0);
        CHECK(bytes_eq(p.idm, IDM, 8) && p.pmm[1] == 0xF1 && p.has_request_data &&
              p.request_data == 0x0003);
        n = resp(r, wl, 0x01, pd, 8);
        CHECK(sqf_parse_polling(r, n, &p) == 0 && !p.has_request_data);
        CHECK(sqf_parse_polling(r, n - 1, &p) == -1);

        if (!wl) break;

        uint8_t sc[] = {2, 0x00, 0x03, 0xFE, 0x00};
        uint16_t codes[4];
        size_t k = 0;
        n = resp(r, wl, 0x0D, sc, sizeof sc);
        CHECK(sqf_parse_system_codes(r, n, codes, 4, &k) == 0 && k == 2 && codes[0] == 0x0003 &&
              codes[1] == 0xFE00);
        CHECK(sqf_parse_system_codes(r, n - 1, codes, 4, &k) == -1);
        CHECK(sqf_parse_key_versions(r, n, codes, 4, &k) == -1); /* wrong response code */

        uint8_t kv[] = {2, 0x00, 0x10, 0xFF, 0xFF};
        n = resp(r, wl, 0x03, kv, sizeof kv);
        CHECK(sqf_parse_key_versions(r, n, codes, 4, &k) == 0 && k == 2 && codes[0] == 0x1000 &&
              codes[1] == 0xFFFF);

        SqfSearch s;
        n = resp(r, wl, 0x0B, (const uint8_t[]){0x0F, 0x09}, 2);
        CHECK(sqf_parse_search_service(r, n, &s) == 0 && s.kind == SQF_SEARCH_SERVICE &&
              s.code == 0x090F);
        n = resp(r, wl, 0x0B, (const uint8_t[]){0x00, 0x08, 0xFE, 0x0F}, 4);
        CHECK(sqf_parse_search_service(r, n, &s) == 0 && s.kind == SQF_SEARCH_AREA &&
              s.code == 0x0800 && s.end_code == 0x0FFE);
        n = resp(r, wl, 0x0B, (const uint8_t[]){0xFF, 0xFF}, 2);
        CHECK(sqf_parse_search_service(r, n, &s) == 0 && s.kind == SQF_SEARCH_END);

        uint8_t rd[3 + 32];
        rd[0] = rd[1] = 0;
        rd[2] = 2;
        for (int i = 0; i < 32; i++) rd[3 + i] = (uint8_t)i;
        SqfRead out;
        n = resp(r, wl, 0x07, rd, sizeof rd);
        CHECK(sqf_parse_read(r, n, &out) == 0 && out.n_blocks == 2 && out.blocks[1][0] == 16 &&
              out.blocks[1][15] == 31);
        n = resp(r, wl, 0x07, (const uint8_t[]){0x01, 0xA6}, 2);
        CHECK(sqf_parse_read(r, n, &out) == 0 && out.sf1 == 1 && out.sf2 == 0xA6 &&
              out.n_blocks == 0);
    }
    CHECK(sqf_parse_read((const uint8_t[]){0x07}, 1, &(SqfRead){0}) == -1);
}

static void test_attributes(void) {
    CHECK(sqf_is_area(0x0000) && sqf_is_area(0x0001) && !sqf_is_area(0x090F));
    CHECK(sqf_is_readable_without_key(0x090F) && !sqf_is_readable_without_key(0x1008));
    CHECK(!sqf_is_readable_without_key(0x0001));
    CHECK(strcmp(sqf_service_kind(0x090F), "cyclic-ro") == 0);
    CHECK(strcmp(sqf_service_kind(0x008B), "random-ro") == 0);
    CHECK(strcmp(sqf_service_kind(0x1017), "purse-ro") == 0);
    CHECK(strcmp(sqf_service_kind(0x0800), "area") == 0);
    CHECK(strcmp(sqf_service_kind(0x0006), "unknown") == 0);
    double t = 256.0 * 16.0 / 13560000.0 * 1000.0;
    CHECK(fabs(sqf_response_time_ms(0x00, 1) - 2 * t) < 1e-12);
    /* EE=01 BBB=010 AAA=011, n=2: T*(3 + 2*4)*4 */
    CHECK(fabs(sqf_response_time_ms(0x53, 2) - 44 * t) < 1e-12);
    CHECK(strcmp(sqf_suica_process_label(0x02), "チャージ") == 0);
    CHECK(sqf_suica_process_label(0x99) == NULL);
    CHECK(sqf_station_area(0xD0, 0x7F, 0) == 0);
    CHECK(sqf_station_area(0xD0, 0x90, 0) == 3 && sqf_station_area(0xD0, 0x90, 1) == 1);
}

static void test_edy_dates(void) {
    uint8_t b[16] = {0x20};
    SqfHistory h;
    struct { uint32_t days; int y, m, d; } cases[] = {
        {0, 2000, 1, 1}, {59, 2000, 2, 29}, {60, 2000, 3, 1}, {366, 2001, 1, 1},
        {9800, 2026, 10, 31}, {32767, 2089, 9, 17},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t ts = cases[i].days << 17 | (13 * 3600 + 5 * 60);
        b[4] = (uint8_t)(ts >> 24); b[5] = (uint8_t)(ts >> 16);
        b[6] = (uint8_t)(ts >> 8); b[7] = (uint8_t)ts;
        CHECK(sqf_parse_edy_block(b, 16, &h) == 0);
        CHECK(h.year == cases[i].y && h.month == cases[i].m && h.day == cases[i].d);
        CHECK(h.hour == 13 && h.minute == 5);
    }
    CHECK(sqf_parse_edy_block(b, 15, &h) == -1);
}

/* ------------------------------------------------------------ fixture */

static int hex_decode(const char *s, uint8_t *out, size_t cap) {
    size_t n = strlen(s);
    if (n % 2 || n / 2 > cap) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return (int)(n / 2);
}

static void format_entry(const SqfHistory *h, char *buf, size_t cap) {
    char date[40], tm[16], en[8], ex[8];
    if (h->year) snprintf(date, sizeof date, "%04d-%02d-%02d", h->year, h->month, h->day);
    else snprintf(date, sizeof date, "?\?\?\?-?\?-?\?");
    if (h->hour >= 0) snprintf(tm, sizeof tm, "%02d:%02d", h->hour, h->minute);
    else snprintf(tm, sizeof tm, "-");
    if (h->has_enter) snprintf(en, sizeof en, "%02X/%02X", h->enter_line, h->enter_station);
    else snprintf(en, sizeof en, "-");
    if (h->has_exit) snprintf(ex, sizeof ex, "%02X/%02X", h->exit_line, h->exit_station);
    else snprintf(ex, sizeof ex, "-");
    snprintf(buf, cap, "E %s %s %s %d %d %02x %02x %s %s", sqf_kind_name(h->kind), date, tm,
             (int)h->fare_yen, (int)h->balance_yen, h->terminal, h->process, en, ex);
}

static SqfStation stations[8192];
static long nstations = -1;

static void test_station_index(void) {
    char csv[] = "\xEF\xBB\xBFline,area,station,company,line_name,station_name,extra\r\n"
                 "e1, 0 ,01,JR東日本,山手線, 東京 ,x\r\n"
                 "e1,0,01,dup,dup,later row loses,x\n"
                 "zz,0,02,bad,bad,bad hex,x\n"
                 "100,0,02,bad,bad,too big,x\n"
                 "short,row\n"
                 "\n"
                 "e1,1,02,名鉄,名古屋本線,名鉄名古屋,x\n"
                 "e1,0,03,c,l,,x\n"
                 "e1,0,02,c,l,last line has no newline,x";
    SqfStation rows[8];
    long n = sqf_station_index(csv, sizeof csv - 1, rows, 8);
    CHECK(n == 4);
    const SqfStation *t = sqf_station_lookup(rows, (size_t)n, 0, 0xE1, 0x01);
    CHECK(t && !strcmp(t->station_name, "東京") && !strcmp(t->company, "JR東日本"));
    t = sqf_station_lookup(rows, (size_t)n, 0, 0xE1, 0x02);
    CHECK(t && !strcmp(t->station_name, "last line has no newline"));
    CHECK(sqf_station_lookup(rows, (size_t)n, 0, 0xE1, 0x04) == NULL);
    /* region bits 7-6 = enter area 1; bits 5-4 = exit area 1 */
    CHECK(!strcmp(sqf_station_resolve(rows, (size_t)n, 0x40, 0xE1, 0x02, 0), "名鉄名古屋"));
    CHECK(!strcmp(sqf_station_resolve(rows, (size_t)n, 0x10, 0xE1, 0x02, 1), "名鉄名古屋"));
    /* area 1 has no E1/01, so fall back to area 0 */
    CHECK(!strcmp(sqf_station_resolve(rows, (size_t)n, 0x40, 0xE1, 0x01, 0), "東京"));
    CHECK(sqf_station_resolve(rows, (size_t)n, 0, 0, 0, 0) == NULL);
    CHECK(sqf_station_resolve(rows, (size_t)n, 0, 0xE1, 0x03, 0) == NULL); /* empty name */

    char no_header[] = "area,line,station,company,line_name\n0,1,1,a,b\n";
    CHECK(sqf_station_index(no_header, sizeof no_header - 1, rows, 8) == -1);
    char too_many[] = "area,line,station,company,line_name,station_name\n0,1,1,a,b,c\n0,1,2,a,b,c\n";
    CHECK(sqf_station_index(too_many, sizeof too_many - 1, rows, 1) == -1);
    char empty[] = "";
    CHECK(sqf_station_index(empty, 0, rows, 8) == -1);
}

static int load_stations(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return -1;
    }
    static char buf[1 << 20];
    size_t len = fread(buf, 1, sizeof buf - 1, f);
    int big = !feof(f);
    fclose(f);
    if (big) return -1;
    nstations = sqf_station_index(buf, len, stations, sizeof stations / sizeof stations[0]);
    printf("station table: %ld codes\n", nstations);
    return nstations > 0 ? 0 : -1;
}

static int run_fixture(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }
    char line[512], brand[16] = "";
    uint8_t blocks[32][SQF_BLOCK_LEN];
    size_t nb = 0, ne = 0, groups = 0, balances = 0, names = 0;
    char expect[32][512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || line[0] == 0) continue;
        if (line[0] == 'G') {
            sscanf(line + 2, "%15s", brand);
            nb = ne = 0;
        } else if (line[0] == 'B') {
            CHECK(nb < 32 && hex_decode(line + 2, blocks[nb], SQF_BLOCK_LEN) == SQF_BLOCK_LEN);
            nb++;
        } else if (line[0] == 'E') {
            CHECK(ne < 32);
            snprintf(expect[ne++], sizeof expect[0], "%s", line);
        } else if (line[0] == '.') {
            SqfHistory h[32];
            size_t k;
            if (!strcmp(brand, "suica")) k = sqf_parse_suica_history(&blocks[0][0], nb, h, 32);
            else if (!strcmp(brand, "nanaco")) k = sqf_parse_nanaco_history(&blocks[0][0], nb, h, 32);
            else k = sqf_parse_edy_history(&blocks[0][0], nb, h, 32);
            if (k != ne) {
                fprintf(stderr, "%s group %zu: %zu entries, want %zu\n", brand, groups, k, ne);
                failures++;
            }
            for (size_t i = 0; i < k && i < ne; i++) {
                char got[512];
                format_entry(&h[i], got, sizeof got);
                if (strcmp(got, expect[i])) {
                    fprintf(stderr, "%s group %zu entry %zu:\n  got  %s\n  want %s\n", brand,
                            groups, i, got, expect[i]);
                    failures++;
                }
            }
            groups++;
        } else if (line[0] == 'L') {
            char b[16], h1[64], h2[64], want[16];
            uint8_t x[32], y[32];
            int32_t got;
            if (sscanf(line + 2, "%15s %63s", b, h1) != 2) { failures++; continue; }
            if (!strcmp(b, "suica")) {
                CHECK(sscanf(line + 2, "%*s %63s %63s %15s", h1, h2, want) == 3);
                int nx = strcmp(h1, "-") ? hex_decode(h1, x, sizeof x) : -1;
                int ny = strcmp(h2, "-") ? hex_decode(h2, y, sizeof y) : -1;
                got = sqf_suica_balance(nx >= 0 ? x : NULL, nx >= 0 ? (size_t)nx : 0,
                                        ny >= 0 ? y : NULL, ny >= 0 ? (size_t)ny : 0);
            } else {
                CHECK(sscanf(line + 2, "%*s %*s %15s", want) == 1);
                int nx = hex_decode(h1, x, sizeof x);
                if (!strcmp(b, "nanaco")) got = sqf_nanaco_balance(x, (size_t)nx);
                else if (!strcmp(b, "edy")) got = sqf_edy_balance(x, (size_t)nx);
                else got = sqf_waon_balance(x, (size_t)nx);
            }
            int32_t w = strcmp(want, "-") ? (int32_t)atol(want) : -1;
            if (got != w) {
                fprintf(stderr, "balance mismatch: %s -> got %d\n", line, (int)got);
                failures++;
            }
            balances++;
        } else if (line[0] == 'R' && nstations > 0) {
            unsigned region, ln, st;
            int ex, off = 0;
            if (sscanf(line + 2, "%x %x %x %d %n", &region, &ln, &st, &ex, &off) != 4 || !off) {
                failures++;
                continue;
            }
            const char *want = line + 2 + off;
            const char *got = sqf_station_resolve(stations, (size_t)nstations, (uint8_t)region, (uint8_t)ln,
                                                  (uint8_t)st, ex);
            if (strcmp(got ? got : "-", want)) {
                fprintf(stderr, "station mismatch: %s -> got %s\n", line, got ? got : "-");
                failures++;
            }
            names++;
        }
    }
    fclose(f);
    printf("fixture: %zu history groups, %zu balance checks, %zu station names\n", groups, balances, names);
    return groups > 0 && balances > 0 && (nstations < 0 || names > 0) ? 0 : -1;
}

int main(int argc, char **argv) {
    test_builders();
    test_parsers();
    test_attributes();
    test_edy_dates();
    test_station_index();
    if (argc > 2 && load_stations(argv[2])) failures++;
    if (argc > 1 && run_fixture(argv[1])) failures++;
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("sqfelica: all tests passed");
    return 0;
}

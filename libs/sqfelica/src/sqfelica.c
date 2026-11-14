#include "sqfelica.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- frames */

static int begin(uint8_t *out, size_t cap, size_t need, uint8_t code) {
    if (!out || cap < need || need > 255) return -1;
    out[0] = (uint8_t)need;
    out[1] = code;
    return 0;
}

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

int sqf_polling(uint8_t *out, size_t cap, uint16_t system_code, uint8_t request_code,
                uint8_t time_slots) {
    if (begin(out, cap, 6, SQF_CMD_POLLING)) return -1;
    out[2] = (uint8_t)(system_code >> 8);
    out[3] = (uint8_t)(system_code & 0xFF);
    out[4] = request_code;
    out[5] = time_slots;
    return 6;
}

static int idm_only(uint8_t *out, size_t cap, uint8_t code, const uint8_t *idm) {
    if (!idm || begin(out, cap, 10, code)) return -1;
    memcpy(out + 2, idm, SQF_IDM_LEN);
    return 10;
}

int sqf_request_system_code(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN]) {
    return idm_only(out, cap, SQF_CMD_REQUEST_SYSTEM_CODE, idm);
}

int sqf_request_response(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN]) {
    return idm_only(out, cap, SQF_CMD_REQUEST_RESPONSE, idm);
}

int sqf_search_service_code(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN],
                            uint16_t index) {
    if (!idm || begin(out, cap, 12, SQF_CMD_SEARCH_SERVICE)) return -1;
    memcpy(out + 2, idm, SQF_IDM_LEN);
    put_le16(out + 10, index);
    return 12;
}

int sqf_request_service(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN],
                        const uint16_t *codes, size_t n) {
    if (!idm || !codes || n < 1 || n > 32) return -1;
    size_t need = 11 + 2 * n;
    if (begin(out, cap, need, SQF_CMD_REQUEST_SERVICE)) return -1;
    memcpy(out + 2, idm, SQF_IDM_LEN);
    out[10] = (uint8_t)n;
    for (size_t i = 0; i < n; i++) put_le16(out + 11 + 2 * i, codes[i]);
    return (int)need;
}

int sqf_block_list_element(uint8_t *out, size_t cap, uint16_t block) {
    if (block <= 0xFF) {
        if (cap < 2) return -1;
        out[0] = 0x80;
        out[1] = (uint8_t)block;
        return 2;
    }
    if (cap < 3) return -1;
    out[0] = 0x00;
    put_le16(out + 1, block);
    return 3;
}

int sqf_read_without_encryption(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN],
                                uint16_t service_code, const uint16_t *blocks, size_t n) {
    if (!idm || !blocks || n < 1 || n > SQF_MAX_READ_BLOCKS || !out) return -1;
    size_t need = 14;
    for (size_t i = 0; i < n; i++) need += blocks[i] <= 0xFF ? 2 : 3;
    if (begin(out, cap, need, SQF_CMD_READ)) return -1;
    memcpy(out + 2, idm, SQF_IDM_LEN);
    out[10] = 0x01; /* one service */
    put_le16(out + 11, service_code);
    out[13] = (uint8_t)n;
    size_t off = 14;
    for (size_t i = 0; i < n; i++) off += (size_t)sqf_block_list_element(out + off, cap - off, blocks[i]);
    return (int)off;
}

/* -------------------------------------------------------------- responses */

int sqf_parse_response(const uint8_t *resp, size_t len, uint8_t expected_code,
                       uint8_t idm_out[SQF_IDM_LEN], const uint8_t **data, size_t *data_len) {
    if (!resp || len < 2) return -1;
    size_t off = 0;
    if (resp[0] == len || (size_t)resp[0] + 1 == len) off = 1;
    if (off + 1 + SQF_IDM_LEN > len) return -1;
    if (resp[off] != expected_code) return -1;
    if (idm_out) memcpy(idm_out, resp + off + 1, SQF_IDM_LEN);
    if (data) *data = resp + off + 1 + SQF_IDM_LEN;
    if (data_len) *data_len = len - (off + 1 + SQF_IDM_LEN);
    return 0;
}

int sqf_parse_polling(const uint8_t *resp, size_t len, SqfPolling *out) {
    const uint8_t *d;
    size_t n;
    if (!out || sqf_parse_response(resp, len, SQF_CMD_POLLING + 1, out->idm, &d, &n)) return -1;
    if (n < 8) return -1;
    memcpy(out->pmm, d, 8);
    out->has_request_data = n >= 10;
    out->request_data = out->has_request_data ? (uint16_t)((d[8] << 8) | d[9]) : 0;
    return 0;
}

static int parse_u16_list(const uint8_t *resp, size_t len, uint8_t code, int big_endian,
                          uint16_t *vals, size_t cap, size_t *count) {
    const uint8_t *d;
    size_t n;
    if (sqf_parse_response(resp, len, code, NULL, &d, &n) || n < 1) return -1;
    size_t k = d[0];
    if (n < 1 + 2 * k) return -1;
    for (size_t i = 0; i < k && i < cap; i++) {
        const uint8_t *p = d + 1 + 2 * i;
        vals[i] = big_endian ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)(p[0] | (p[1] << 8));
    }
    if (count) *count = k;
    return 0;
}

int sqf_parse_system_codes(const uint8_t *resp, size_t len, uint16_t *codes, size_t cap,
                           size_t *n) {
    return parse_u16_list(resp, len, SQF_CMD_REQUEST_SYSTEM_CODE + 1, 1, codes, cap, n);
}

int sqf_parse_key_versions(const uint8_t *resp, size_t len, uint16_t *versions, size_t cap,
                           size_t *n) {
    return parse_u16_list(resp, len, SQF_CMD_REQUEST_SERVICE + 1, 0, versions, cap, n);
}

int sqf_parse_search_service(const uint8_t *resp, size_t len, SqfSearch *out) {
    const uint8_t *d;
    size_t n;
    if (!out || sqf_parse_response(resp, len, SQF_CMD_SEARCH_SERVICE + 1, NULL, &d, &n) || n < 2)
        return -1;
    uint16_t code = (uint16_t)(d[0] | (d[1] << 8));
    out->code = code;
    out->end_code = 0;
    if (code == 0xFFFF) {
        out->kind = SQF_SEARCH_END;
    } else if (n >= 4) {
        out->kind = SQF_SEARCH_AREA;
        out->end_code = (uint16_t)(d[2] | (d[3] << 8));
    } else {
        out->kind = SQF_SEARCH_SERVICE;
    }
    return 0;
}

int sqf_parse_read(const uint8_t *resp, size_t len, SqfRead *out) {
    const uint8_t *d;
    size_t n;
    if (!out || sqf_parse_response(resp, len, SQF_CMD_READ + 1, NULL, &d, &n) || n < 2) return -1;
    out->sf1 = d[0];
    out->sf2 = d[1];
    out->n_blocks = 0;
    if (d[0] || d[1] || n < 3) return 0;
    size_t k = d[2];
    for (size_t i = 0; i < k && i < SQF_MAX_READ_BLOCKS; i++) {
        size_t s = 3 + SQF_BLOCK_LEN * i;
        if (s + SQF_BLOCK_LEN > n) break;
        memcpy(out->blocks[out->n_blocks++], d + s, SQF_BLOCK_LEN);
    }
    return 0;
}

/* ------------------------------------------------------ attributes / PMm */

int sqf_is_area(uint16_t code) { return ((code & 0x3F) & 0x3E) == 0; }

int sqf_is_readable_without_key(uint16_t code) {
    return !sqf_is_area(code) && (code & 0x01);
}

const char *sqf_service_kind(uint16_t code) {
    switch (code & 0x3E) {
        case 0x00: return "area";
        case 0x08: return "random-rw";
        case 0x0A: return "random-ro";
        case 0x0C: return "cyclic-rw";
        case 0x0E: return "cyclic-ro";
        case 0x10: return "purse-direct";
        case 0x12: return "purse-cashback";
        case 0x14: return "purse-decrement";
        case 0x16: return "purse-ro";
        default: return "unknown";
    }
}

double sqf_response_time_ms(uint8_t param, unsigned n) {
    const double t_ms = 256.0 * 16.0 / 13560000.0 * 1000.0;
    unsigned a = param & 0x07, b = (param >> 3) & 0x07, e = (param >> 6) & 0x03;
    double pow4 = 1.0;
    for (unsigned i = 0; i < e; i++) pow4 *= 4.0;
    return t_ms * ((double)(b + 1) + (double)n * (double)(a + 1)) * pow4;
}

/* ---------------------------------------------------------------- history */

const char *sqf_kind_name(SqfKind k) {
    switch (k) {
        case SQF_KIND_TRAIN: return "train";
        case SQF_KIND_BUS: return "bus";
        case SQF_KIND_SALE: return "sale";
        case SQF_KIND_CHARGE: return "charge";
        default: return "other";
    }
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int bcd(uint8_t v) { return ((v >> 4) & 0xF) * 10 + (v & 0xF); }

static int all_zero(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (b[i]) return 0;
    return 1;
}

static int32_t clamp_yen(uint32_t v) { return v > 500000u ? 500000 : (int32_t)v; }

static void history_init(SqfHistory *h) {
    memset(h, 0, sizeof *h);
    h->hour = h->minute = -1;
}

int sqf_station_area(uint8_t region, uint8_t line, int for_exit) {
    if (line < 0x80) return 0;
    return (region >> (for_exit ? 4 : 6)) & 0x3;
}

/* ---- station table ---- */

enum { COL_AREA, COL_LINE, COL_STATION, COL_COMPANY, COL_LINE_NAME, COL_STATION_NAME, NCOLS };
static const char *const station_cols[NCOLS] = {"area", "line", "station", "company", "line_name", "station_name"};

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* Split one line [p, end) at commas into NUL-terminated, trimmed fields. */
static int split_fields(char *p, char *end, char **f, int max) {
    int n = 0;
    for (;;) {
        char *start = p;
        while (p < end && *p != ',') p++;
        char *stop = p;
        while (start < stop && is_space(*start)) start++;
        while (stop > start && is_space(stop[-1])) stop--;
        if (n < max) f[n] = start;
        n++;
        int last = p >= end;
        *stop = 0; /* stop <= p, and p is a comma or the line's NUL */
        if (last) return n;
        p++;
    }
}

static int hex_byte(const char *s) {
    int v = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        int d = (*s >= '0' && *s <= '9') ? *s - '0'
              : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10
              : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v * 16 + d;
        if (v > 0xFF) return -1;
    }
    return v;
}

static uint32_t station_key(uint8_t area, uint8_t line, uint8_t station) {
    return (uint32_t)area << 16 | (uint32_t)line << 8 | station;
}

static int cmp_station(const void *a, const void *b) {
    const SqfStation *x = a, *y = b;
    uint32_t kx = station_key(x->area, x->line, x->station), ky = station_key(y->area, y->line, y->station);
    if (kx != ky) return kx < ky ? -1 : 1;
    /* Same code: earlier in the file first. Fields point into one buffer in file order. */
    return x->company < y->company ? -1 : x->company > y->company;
}

long sqf_station_index(char *csv, size_t len, SqfStation *rows, size_t cap) {
    char *p = csv, *end = csv + len;
    if (len >= 3 && (uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) p += 3;
    int col[NCOLS], header = 1, ncol = 0;
    size_t n = 0;
    while (p < end) {
        char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        char *next = nl < end ? nl + 1 : end;
        char *f[16];
        *nl = 0; /* csv[len] is writable too, for a last line without a newline */
        int nf = split_fields(p, nl, f, 16);
        if (nf > 16) nf = 16;
        if (header) {
            for (int c = 0; c < NCOLS; c++) {
                col[c] = -1;
                for (int i = 0; i < nf; i++)
                    if (strcmp(f[i], station_cols[c]) == 0) col[c] = i;
                if (col[c] < 0) return -1;
                if (col[c] + 1 > ncol) ncol = col[c] + 1;
            }
            header = 0;
        } else if (nf >= ncol) {
            int a = hex_byte(f[col[COL_AREA]]), l = hex_byte(f[col[COL_LINE]]), s = hex_byte(f[col[COL_STATION]]);
            if (a >= 0 && l >= 0 && s >= 0) {
                if (n >= cap) return -1;
                rows[n].area = (uint8_t)a;
                rows[n].line = (uint8_t)l;
                rows[n].station = (uint8_t)s;
                rows[n].company = f[col[COL_COMPANY]];
                rows[n].line_name = f[col[COL_LINE_NAME]];
                rows[n].station_name = f[col[COL_STATION_NAME]];
                n++;
            }
        }
        p = next;
    }
    if (header) return -1;
    qsort(rows, n, sizeof *rows, cmp_station);
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (w && station_key(rows[w - 1].area, rows[w - 1].line, rows[w - 1].station) ==
                     station_key(rows[i].area, rows[i].line, rows[i].station))
            continue;
        rows[w++] = rows[i];
    }
    return (long)w;
}

const SqfStation *sqf_station_lookup(const SqfStation *rows, size_t n, uint8_t area, uint8_t line,
                                     uint8_t station) {
    uint32_t key = station_key(area, line, station);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t k = station_key(rows[mid].area, rows[mid].line, rows[mid].station);
        if (k == key) return &rows[mid];
        if (k < key) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

const char *sqf_station_resolve(const SqfStation *rows, size_t n, uint8_t region, uint8_t line,
                                uint8_t station, int for_exit) {
    if (line == 0 && station == 0) return NULL;
    int area = sqf_station_area(region, line, for_exit);
    const SqfStation *r = sqf_station_lookup(rows, n, (uint8_t)area, line, station);
    if (r && r->station_name[0]) return r->station_name;
    if (area != 0) {
        r = sqf_station_lookup(rows, n, 0, line, station);
        if (r && r->station_name[0]) return r->station_name;
    }
    return NULL;
}

static int in_list(uint8_t v, const uint8_t *list, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (list[i] == v) return 1;
    return 0;
}

int sqf_parse_suica_block(const uint8_t *b, size_t len, SqfHistory *h) {
    if (!b || !h || len < SQF_BLOCK_LEN || all_zero(b, SQF_BLOCK_LEN)) return -1;
    history_init(h);
    h->terminal = b[0];
    h->process = b[1];
    unsigned dw = ((unsigned)b[4] << 8) | b[5];
    int y = 2000 + (int)(dw >> 9), m = (int)((dw >> 5) & 0xF), d = (int)(dw & 0x1F);
    if (!(m >= 1 && m <= 12 && d >= 1 && d <= 31 && y <= 2099)) {
        /* Some terminals write BCD yy mm dd instead (kept as-is, like the Python parser). */
        y = 2000 + bcd(b[4]);
        m = bcd(b[5]);
        d = bcd(b[6]);
    }
    h->year = y;
    h->month = m;
    h->day = d;

    static const uint8_t bus_proc[] = {0x0D, 0x0F, 0x1F, 0x23};
    static const uint8_t sale_proc[] = {0x46, 0x49, 0x4A, 0x4B, 0xC6, 0xCB};
    static const uint8_t train_proc[] = {0x01, 0x03, 0x04, 0x05, 0x06, 0x08,
                                         0x13, 0x14, 0x15, 0x84, 0x85};
    /* Shop sales carry a time and terminal ID in bytes 6-9, not stations: terminal
      * types C7/C8, and any non-bus sale process (real dumps show CA with process 46).
      * Bus (terminal 0x05 or bus process): bytes 6-9 are operator + stop, never rail. */
    int bus = b[0] == 0x05 || in_list(b[1], bus_proc, sizeof bus_proc);
    int product = b[0] == 0xC7 || b[0] == 0xC8 || (in_list(b[1], sale_proc, sizeof sale_proc) && !bus);
    int charge = b[1] == 0x02 || b[1] == 0x48;
    h->settlement = b[2];
    if (charge) h->kind = SQF_KIND_CHARGE;
    else if (bus) h->kind = SQF_KIND_BUS;
    else if (product) h->kind = SQF_KIND_SALE;
    else if (in_list(b[1], train_proc, sizeof train_proc)) h->kind = SQF_KIND_TRAIN;
    else h->kind = SQF_KIND_OTHER;

    if (bus) {
        h->bus_operator = (uint16_t)(((unsigned)b[6] << 8) | b[7]);
        h->bus_stop = (uint16_t)(((unsigned)b[8] << 8) | b[9]);
        /* has_enter/has_exit stay 0 — no station lookup for bus rows. */
    } else if (!product) {
        h->region = b[15];
        if (b[6] || b[7]) {
            h->has_enter = 1;
            h->enter_line = b[6];
            h->enter_station = b[7];
        }
        if (!charge && (b[8] || b[9])) {
            h->has_exit = 1;
            h->exit_line = b[8];
            h->exit_station = b[9];
        }
    } else if (product) {
        unsigned tw = ((unsigned)b[6] << 8) | b[7];
        int hh = (int)(tw >> 11), mm = (int)((tw >> 5) & 0x3F);
        if (hh <= 23 && mm <= 59) {
            h->hour = hh;
            h->minute = mm;
        }
    }
    h->balance_yen = (int32_t)(b[10] | (b[11] << 8));
    return 0;
}

size_t sqf_parse_suica_history(const uint8_t *blocks, size_t n,
                               SqfHistory *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k < cap; i++)
        if (sqf_parse_suica_block(blocks + SQF_BLOCK_LEN * i, SQF_BLOCK_LEN, &out[k]) == 0) k++;
    /* Fare = balance change to the next older entry (felica_parse rules). */
    for (size_t i = 0; i + 1 < k; i++) {
        int32_t delta = out[i + 1].balance_yen - out[i].balance_yen;
        if (delta >= 1 && delta <= 20000) out[i].fare_yen = delta;
        else if (out[i].kind == SQF_KIND_CHARGE && -delta >= 1 && -delta <= 50000)
            out[i].fare_yen = -delta;
    }
    return k;
}

int sqf_parse_nanaco_block(const uint8_t *b, size_t len, SqfHistory *h) {
    if (!b || !h || len < SQF_BLOCK_LEN || all_zero(b, SQF_BLOCK_LEN) || b[0] == 0) return -1;
    history_init(h);
    uint8_t typ = b[0];
    h->terminal = h->process = typ;
    uint32_t amount = be32(b + 1);
    h->balance_yen = clamp_yen(be32(b + 5));
    uint32_t packed = be32(b + 9);
    int m = (int)((packed >> 17) & 0xF), d = (int)((packed >> 12) & 0x1F);
    if (m >= 1 && m <= 12 && d >= 1 && d <= 31) {
        h->year = 2000 + (int)((packed >> 21) & 0x7FF);
        h->month = m;
        h->day = d;
    }
    int hh = (int)((packed >> 6) & 0x3F), mm = (int)(packed & 0x3F);
    if (hh <= 23 && mm <= 59) {
        h->hour = hh;
        h->minute = mm;
    }
    if (typ == 0x47) h->kind = SQF_KIND_SALE;
    else if (typ == 0x5C || typ == 0x6F || typ == 0x70) h->kind = SQF_KIND_CHARGE;
    else h->kind = SQF_KIND_OTHER;
    h->amount_yen = amount > 0x7FFFFFFFu ? 0x7FFFFFFF : (int32_t)amount;
    if (h->kind == SQF_KIND_SALE && amount >= 1 && amount <= 100000) h->fare_yen = (int32_t)amount;
    return 0;
}

/* Civil date from days since 2000-01-01 (proleptic Gregorian). */
static void civil_from_days2000(uint32_t days, int *y, int *m, int *d) {
    /* Shift to days since 0000-03-01: 2000-01-01 is day 730425 of that era count. */
    long z = (long)days + 730425L;
    long era = z / 146097L;
    long doe = z - era * 146097L;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

int sqf_parse_edy_block(const uint8_t *b, size_t len, SqfHistory *h) {
    if (!b || !h || len < SQF_BLOCK_LEN || all_zero(b, SQF_BLOCK_LEN) || b[0] == 0) return -1;
    history_init(h);
    uint8_t typ = b[0];
    h->terminal = h->process = typ;
    uint32_t ts = be32(b + 4);
    civil_from_days2000(ts >> 17, &h->year, &h->month, &h->day);
    uint32_t secs = ts & 0x1FFFF;
    h->hour = (int)(secs / 3600);
    h->minute = (int)((secs % 3600) / 60);
    uint32_t amount = be32(b + 8);
    h->balance_yen = clamp_yen(be32(b + 12));
    if (typ == 0x20) h->kind = SQF_KIND_SALE;
    else if (typ == 0x02 || typ == 0x04) h->kind = SQF_KIND_CHARGE;
    else h->kind = SQF_KIND_OTHER;
    h->amount_yen = amount > 0x7FFFFFFFu ? 0x7FFFFFFF : (int32_t)amount;
    if (h->kind == SQF_KIND_SALE && amount >= 1 && amount <= 100000) h->fare_yen = (int32_t)amount;
    return 0;
}

static size_t parse_each(int (*fn)(const uint8_t *, size_t, SqfHistory *),
                         const uint8_t *blocks, size_t n, SqfHistory *out,
                         size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k < cap; i++)
        if (fn(blocks + SQF_BLOCK_LEN * i, SQF_BLOCK_LEN, &out[k]) == 0) k++;
    return k;
}

size_t sqf_parse_nanaco_history(const uint8_t *blocks, size_t n,
                                SqfHistory *out, size_t cap) {
    return parse_each(sqf_parse_nanaco_block, blocks, n, out, cap);
}

size_t sqf_parse_edy_history(const uint8_t *blocks, size_t n,
                             SqfHistory *out, size_t cap) {
    return parse_each(sqf_parse_edy_block, blocks, n, out, cap);
}

/* Pack bits MSB-first from record[18..], matching felica_parse.parse_waon_history_block. */
static int waon_take(const uint8_t *rec, int *bit, int nbits) {
    int v = 0;
    for (int i = 0; i < nbits; i++) {
        int byte_index = 18 + (*bit / 8);
        int bit_index = 7 - (*bit % 8);
        v = (v << 1) | ((rec[byte_index] >> bit_index) & 1);
        (*bit)++;
    }
    return v;
}

int sqf_parse_waon_block(const uint8_t *b, size_t len, SqfHistory *h) {
    if (!b || !h || len < 32 || all_zero(b, 32) || b[17] == 0) return -1;
    history_init(h);
    uint8_t typ = b[17];
    h->terminal = h->process = typ;
    int bit = 0;
    int y = 2005 + waon_take(b, &bit, 5);
    int m = waon_take(b, &bit, 4);
    int d = waon_take(b, &bit, 5);
    int hh = waon_take(b, &bit, 5);
    int mm = waon_take(b, &bit, 6);
    int32_t bal = clamp_yen((uint32_t)waon_take(b, &bit, 18));
    int usage = waon_take(b, &bit, 18);
    int charge = waon_take(b, &bit, 17);
    if (m >= 1 && m <= 12 && d >= 1 && d <= 31 && y >= 2005 && y <= 2099) {
        h->year = y;
        h->month = m;
        h->day = d;
    }
    if (hh <= 23 && mm <= 59) {
        h->hour = hh;
        h->minute = mm;
    }
    h->balance_yen = bal;
    if (typ == 0x04) {
        h->kind = SQF_KIND_SALE;
        h->amount_yen = usage;
        if (usage >= 1 && usage <= 100000) h->fare_yen = usage;
    } else if (typ == 0x0C || typ == 0x10) {
        h->kind = SQF_KIND_CHARGE;
        h->amount_yen = charge;
    } else {
        h->kind = SQF_KIND_OTHER;
        h->amount_yen = usage;
    }
    return 0;
}

size_t sqf_parse_waon_history(const uint8_t *blocks, size_t n,
                              SqfHistory *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i + 1 < n && k < cap; i += 2) {
        uint8_t rec[32];
        memcpy(rec, blocks + SQF_BLOCK_LEN * i, SQF_BLOCK_LEN);
        memcpy(rec + SQF_BLOCK_LEN, blocks + SQF_BLOCK_LEN * (i + 1), SQF_BLOCK_LEN);
        if (sqf_parse_waon_block(rec, 32, &out[k]) == 0) k++;
    }
    return k;
}

/* --------------------------------------------------------------- balances */

int32_t sqf_suica_balance(const uint8_t *b090f, size_t l090f, const uint8_t *b008b,
                          size_t l008b) {
    int has_a = b090f && l090f >= 12, has_b = b008b && l008b >= 13;
    int32_t a = has_a ? (int32_t)(b090f[10] | (b090f[11] << 8)) : 0;
    int32_t b = has_b ? (int32_t)(b008b[11] | (b008b[12] << 8)) : 0;
    int ok_a = has_a && a <= 50000, ok_b = has_b && b <= 50000;
    if (ok_a && ok_b && a > b * 2 && b > 0) return b;
    if (ok_a) return a;
    if (ok_b) return b;
    if (has_a) return a;
    if (has_b) return b;
    return -1;
}

int32_t sqf_nanaco_balance(const uint8_t *b, size_t len) {
    return (b && len >= 4) ? clamp_yen(le32(b)) : -1;
}

int32_t sqf_edy_balance(const uint8_t *b, size_t len) {
    return (b && len >= 4) ? clamp_yen(le32(b)) : -1;
}

int32_t sqf_waon_balance(const uint8_t *b, size_t len) {
    return (b && len >= 2) ? (int32_t)(b[0] | (b[1] << 8)) : -1;
}

const char *sqf_suica_process_label(uint8_t p) {
    switch (p) {
        case 0x01: return "運賃支払";
        case 0x02: return "チャージ";
        case 0x03: return "券購";
        case 0x04: return "精算";
        case 0x05: return "入場精算";
        case 0x06: return "窓出";
        case 0x0D: return "バス";
        case 0x0F: return "バス";
        case 0x13: return "新幹線";
        case 0x14: return "入A";
        case 0x15: return "出A";
        case 0x1F: return "バスチャージ";
        case 0x23: return "バス券購";
        case 0x46: return "物販";
        case 0x48: return "特典チャージ";
        default: return NULL;
    }
}

const char *sqf_suica_family_label(const uint8_t *block_008b, size_t len, int settlement) {
    int card_type = -1, region = -1;
    if (block_008b && len > 8) {
        card_type = (block_008b[8] >> 4) & 0xF;
        region = block_008b[8] & 0xF;
    }
    if (card_type == 3) return "ICOCA";
    if (card_type == 0) return "IruCa";
    if (settlement >= 0 && settlement <= 0xFF) {
        if (settlement == 0x0D) return "PASMO";
        if (settlement == 0x3F) return "モバイルSuica";
        if (settlement == 0x0B) return "PiTaPa";
        if (settlement == 0x02) return "Suica";
    }
    if (card_type == 2 && region == 2) return "PiTaPa";
    if (card_type == 2) return "Suica / PASMO";
    return "Suica / PASMO";
}

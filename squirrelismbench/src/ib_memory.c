#define _POSIX_C_SOURCE 200809L
#include "ib_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Park–Miller "minimal standard" multiplicative congruential generator.
 * Cited as the NR Ch. 7.1 class of portable LCG (a=16807, m=2^31-1).
 * Original Soft implementation — do not paste Numerical Recipes listings.
 */
static unsigned int ib_park_miller(unsigned int *state) {
    const unsigned int a = 16807u;
    const unsigned int m = 2147483647u;
    unsigned long long prod = (unsigned long long)(*state) * (unsigned long long)a;
    *state = (unsigned int)(prod % m);
    if (*state == 0) *state = 1;
    return *state;
}

static unsigned char ib_lcg_byte(unsigned int *state) {
    return (unsigned char)(ib_park_miller(state) & 0xffu);
}

int ib_memory_spd_checksum_ok(const IbMemoryIface *m) {
    int i;
    unsigned char x = 0;
    if (!m || m->spd_len < 2 || m->spd_len > IB_SPD_MAX) return 0;
    for (i = 0; i < m->spd_len - 1; i++) x ^= m->spd[i];
    return x == m->spd[m->spd_len - 1];
}

static int starts_with(const char *s, const char *p) {
    while (*p) {
        if (*s++ != *p++) return 0;
    }
    return 1;
}

int ib_memory_spd_parse_line(const char *line, unsigned char *spd, int *spd_len,
                             int spd_cap, char *err, int err_cap) {
    const char *p;
    int n = 0;
    if (!line || !spd || !spd_len || spd_cap < 2) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (!starts_with(p, "ISM SPD:")) {
        if (err && err_cap) snprintf(err, err_cap, "need ISM SPD: prefix");
        return -1;
    }
    p += 8; /* strlen("ISM SPD:") */
    while (*p == ' ' || *p == '\t') p++;
    while (*p && *p != '\n' && *p != '\r') {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n' || *p == '\r') break;
        /* skip Soft optional keywords addr=… / len=… */
        if ((starts_with(p, "addr=") || starts_with(p, "ADDR=") ||
             starts_with(p, "len=") || starts_with(p, "LEN="))) {
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            continue;
        }
        {
            char *end = NULL;
            unsigned long v = strtoul(p, &end, 16);
            if (end == p) {
                if (err && err_cap) snprintf(err, err_cap, "bad SPD hex");
                return -1;
            }
            if (n >= spd_cap) {
                if (err && err_cap) snprintf(err, err_cap, "SPD too long");
                return -1;
            }
            if (v > 0xfful) {
                if (err && err_cap) snprintf(err, err_cap, "SPD byte out of range");
                return -1;
            }
            spd[n++] = (unsigned char)(v & 0xffu);
            p = end;
        }
    }
    if (n < 2) {
        if (err && err_cap) snprintf(err, err_cap, "SPD needs >= 2 bytes");
        return -1;
    }
    *spd_len = n;
    return 0;
}

int ib_memory_spd_apply_cdc_text(IbMemoryIface *m, const char *text, char *err,
                                 int err_cap) {
    char *buf, *save = NULL, *line;
    unsigned char tmp[IB_SPD_MAX];
    int n = 0, found = 0;
    if (!m || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (!*t || *t == '#') continue;
        if (!starts_with(t, "ISM SPD:")) continue;
        if (ib_memory_spd_parse_line(t, tmp, &n, IB_SPD_MAX, err, err_cap) != 0) {
            free(buf);
            return -1;
        }
        found = 1;
        break;
    }
    free(buf);
    if (!found) {
        if (err && err_cap) snprintf(err, err_cap, "no ISM SPD: line in CDC text");
        return -1;
    }
    memcpy(m->spd, tmp, (size_t)n);
    m->spd_len = n;
    if (!ib_memory_spd_checksum_ok(m)) {
        if (err && err_cap) snprintf(err, err_cap, "SPD Soft XOR contract failed");
        return -1;
    }
    return 0;
}

int ib_memory_spd_apply_cdc_path(IbMemoryIface *m, const char *path, char *err,
                                 int err_cap) {
    FILE *f;
    long n;
    char *text;
    int rc;
    char line[512];
    unsigned char tmp[IB_SPD_MAX];
    int len = 0, found = 0, i;
    if (!m || !path) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        if (err && err_cap)
            snprintf(err, err_cap, "cannot open CDC path %s", path);
        return -1;
    }
    /* Regular Soft stub files are seekable; live ACM often is not. */
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && n <= (1 << 20)) {
        rewind(f);
        text = malloc((size_t)n + 1);
        if (!text) {
            fclose(f);
            if (err && err_cap) snprintf(err, err_cap, "oom");
            return -1;
        }
        if (fread(text, 1, (size_t)n, f) != (size_t)n) {
            free(text);
            fclose(f);
            if (err && err_cap) snprintf(err, err_cap, "read failed on %s", path);
            return -1;
        }
        text[n] = 0;
        fclose(f);
        rc = ib_memory_spd_apply_cdc_text(m, text, err, err_cap);
        free(text);
        return rc;
    }
    /* Non-seekable or oversized: scan lines (live USB-CDC Soft path). */
    clearerr(f);
    rewind(f);
    for (i = 0; i < 64 && fgets(line, (int)sizeof line, f); i++) {
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (!starts_with(t, "ISM SPD:")) continue;
        if (ib_memory_spd_parse_line(t, tmp, &len, IB_SPD_MAX, err, err_cap) !=
            0) {
            fclose(f);
            return -1;
        }
        found = 1;
        break;
    }
    fclose(f);
    if (!found) {
        if (err && err_cap)
            snprintf(err, err_cap, "no ISM SPD: line on %s", path);
        return -1;
    }
    memcpy(m->spd, tmp, (size_t)len);
    m->spd_len = len;
    if (!ib_memory_spd_checksum_ok(m)) {
        if (err && err_cap) snprintf(err, err_cap, "SPD Soft XOR contract failed");
        return -1;
    }
    return 0;
}

int ib_memory_parse(IbMemoryIface *m, const char *text, char *err, int err_cap) {
    if (!m || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(m, 0, sizeof *m);
    m->spd_i2c_addr = 0x50;
    m->spd_len = 16;
    m->dq_width = 8;
    m->banks = 2;
    m->density_kib = 8192;
    m->pattern_bytes = 4096;
    m->lcg_seed = 1u;
    snprintf(m->part, sizeof m->part, "LY68L6400SLIT");

    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int saw = 0, line_no = 0, spd_set = 0;
    while (line) {
        line_no++;
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        char *e = t + strlen(t);
        while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (strncmp(t, "ISM-MEM memory interface", 24) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-MEM memory interface", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[64], val[400];
        if (sscanf(t, "%63[^:]: %399[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        /* trim key/val lightly */
        char *vk = key;
        while (*vk == ' ' || *vk == '\t') vk++;
        char *vv = val;
        while (*vv == ' ' || *vv == '\t') vv++;
        if (strcmp(vk, "part") == 0) snprintf(m->part, sizeof m->part, "%s", vv);
        else if (strcmp(vk, "density_kib") == 0) m->density_kib = atoi(vv);
        else if (strcmp(vk, "banks") == 0) m->banks = atoi(vv);
        else if (strcmp(vk, "spd_i2c_addr") == 0) {
            if (strncmp(vv, "0x", 2) == 0 || strncmp(vv, "0X", 2) == 0)
                m->spd_i2c_addr = (int)strtol(vv, NULL, 16);
            else
                m->spd_i2c_addr = atoi(vv);
        } else if (strcmp(vk, "spd_bytes") == 0) m->spd_len = atoi(vv);
        else if (strcmp(vk, "dq_width") == 0) m->dq_width = atoi(vv);
        else if (strcmp(vk, "pattern_bytes") == 0) m->pattern_bytes = atoi(vv);
        else if (strcmp(vk, "lcg_seed") == 0) m->lcg_seed = (unsigned int)strtoul(vv, NULL, 0);
        else if (strcmp(vk, "spd") == 0) {
            int n = 0;
            const char *p = vv;
            while (*p && n < IB_SPD_MAX) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                char *end = NULL;
                unsigned long v = strtoul(p, &end, 16);
                if (end == p) {
                    if (err && err_cap) snprintf(err, err_cap, "line %d: bad spd hex", line_no);
                    free(buf);
                    return -1;
                }
                m->spd[n++] = (unsigned char)(v & 0xffu);
                p = end;
            }
            m->spd_len = n;
            spd_set = 1;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (!spd_set || m->spd_len < 2 || m->spd_len > IB_SPD_MAX) {
        if (err && err_cap) snprintf(err, err_cap, "spd required (2..%d bytes)", IB_SPD_MAX);
        return -1;
    }
    if (m->banks < 1 || m->banks > 8 || m->density_kib <= 0 || m->dq_width <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "bad memory geometry");
        return -1;
    }
    if (m->pattern_bytes < 16 || m->pattern_bytes > IB_MEM_BANK_MAX) {
        if (err && err_cap) snprintf(err, err_cap, "pattern_bytes out of range");
        return -1;
    }
    if (m->spd_i2c_addr != 0x50) {
        if (err && err_cap) snprintf(err, err_cap, "spd_i2c_addr must be 0x50 (ISM J2/edge)");
        return -1;
    }
    if (m->lcg_seed == 0) m->lcg_seed = 1u;
    return 0;
}

static int fail_pat(IbMemTestResult *res, const char *why) {
    (void)why;
    res->nfail++;
    res->ok = 0;
    return -1;
}

static int run_const(unsigned char *bank, int n, unsigned char v, IbMemTestResult *res) {
    int i;
    for (i = 0; i < n; i++) bank[i] = v;
    for (i = 0; i < n; i++) {
        if (bank[i] != v) return fail_pat(res, "const");
    }
    res->npass++;
    return 0;
}

static int run_walking(unsigned char *bank, int n, int ones, IbMemTestResult *res) {
    int bit, i;
    for (bit = 0; bit < 8; bit++) {
        unsigned char mask = (unsigned char)(1u << bit);
        for (i = 0; i < n; i++) bank[i] = ones ? mask : (unsigned char)~mask;
        for (i = 0; i < n; i++) {
            unsigned char expect = ones ? mask : (unsigned char)~mask;
            if (bank[i] != expect) return fail_pat(res, "walk");
        }
        res->npass++;
    }
    return 0;
}

static int run_checker(unsigned char *bank, int n, IbMemTestResult *res) {
    int i;
    for (i = 0; i < n; i++) bank[i] = (i & 1) ? 0xAAu : 0x55u;
    for (i = 0; i < n; i++) {
        unsigned char expect = (i & 1) ? 0xAAu : 0x55u;
        if (bank[i] != expect) return fail_pat(res, "checker");
    }
    res->npass++;
    for (i = 0; i < n; i++) bank[i] = (i & 1) ? 0x55u : 0xAAu;
    for (i = 0; i < n; i++) {
        unsigned char expect = (i & 1) ? 0x55u : 0xAAu;
        if (bank[i] != expect) return fail_pat(res, "checker2");
    }
    res->npass++;
    return 0;
}

static int run_lcg(unsigned char *bank, int n, unsigned int seed, IbMemTestResult *res) {
    unsigned int s = seed ? seed : 1u;
    int i;
    for (i = 0; i < n; i++) bank[i] = ib_lcg_byte(&s);
    s = seed ? seed : 1u;
    for (i = 0; i < n; i++) {
        unsigned char expect = ib_lcg_byte(&s);
        if (bank[i] != expect) return fail_pat(res, "lcg");
    }
    res->npass++;
    return 0;
}

int ib_memory_selftest(const IbMemoryIface *m, IbMemTestResult *res) {
    unsigned char *bank;
    int n;
    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!ib_memory_spd_checksum_ok(m)) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary,
                 "memory selftest part=%s spd_xor FAIL → FAIL", m->part);
        return 0;
    }
    res->npass++; /* SPD XOR contract */

    n = m->pattern_bytes;
    bank = malloc((size_t)n);
    if (!bank) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "memory selftest oom → FAIL");
        return 0;
    }

    if (run_const(bank, n, 0x00, res) != 0) goto done;
    if (run_const(bank, n, 0xff, res) != 0) goto done;
    if (run_walking(bank, n, 1, res) != 0) goto done;
    if (run_walking(bank, n, 0, res) != 0) goto done;
    if (run_checker(bank, n, res) != 0) goto done;
    if (run_lcg(bank, n, m->lcg_seed, res) != 0) goto done;

done:
    free(bank);
    snprintf(res->summary, sizeof res->summary,
             "memory selftest part=%s banks=%d spd_len=%d patterns_ok=%d fail=%d "
             "(NR Ch.7.1 LCG oracle) → %s",
             m->part, m->banks, m->spd_len, res->npass, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}

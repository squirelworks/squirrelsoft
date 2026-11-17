#define _POSIX_C_SOURCE 200809L
#include "ib_mem_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int starts_with(const char *s, const char *p) {
    while (*p) {
        if (*s++ != *p++) return 0;
    }
    return 1;
}

static int looks_like_live_cdc(const char *ep) {
    if (!ep) return 0;
    if (starts_with(ep, "/dev/ttyACM")) return 1;
    if (starts_with(ep, "/dev/ttyUSB")) return 1;
    if (starts_with(ep, "ttyACM:")) return 1;
    if (starts_with(ep, "ttyUSB:")) return 1;
    return 0;
}

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

static int fail_pat(IbMemTestResult *res) {
    res->nfail++;
    res->ok = 0;
    return -1;
}

static int open_stub(IbMemLive *live, int size, char *err, int err_cap) {
    if (size < 16 || size > IB_MEM_BANK_MAX) {
        if (err && err_cap) snprintf(err, err_cap, "stub size out of range");
        return -1;
    }
    memset(live, 0, sizeof *live);
    live->data = calloc(1, (size_t)size);
    if (!live->data) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    live->size = size;
    live->owned = 1;
    snprintf(live->endpoint, sizeof live->endpoint, "stub");
    return 0;
}

static int open_sqmem(IbMemLive *live, const char *path, int size_hint, char *err,
                      int err_cap) {
    FILE *f = fopen(path, "rb");
    char line[256];
    int size = size_hint > 0 ? size_hint : 4096;
    int saw = 0;
    if (!f) {
        if (err && err_cap) snprintf(err, err_cap, "cannot open %s", path);
        return -1;
    }
    while (fgets(line, (int)sizeof line, f)) {
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (!*t || *t == '#') continue;
        if (!saw) {
            if (strncmp(t, "ISM-MEM-LIVE stub", 16) != 0) {
                fclose(f);
                if (err && err_cap)
                    snprintf(err, err_cap, "need ISM-MEM-LIVE stub header");
                return -1;
            }
            saw = 1;
            continue;
        }
        {
            char key[64], val[128];
            if (sscanf(t, "%63[^:]: %127[^\n]", key, val) == 2) {
                char *vk = key;
                char *vv = val;
                while (*vk == ' ' || *vk == '\t') vk++;
                while (*vv == ' ' || *vv == '\t') vv++;
                if (strcmp(vk, "size") == 0) size = atoi(vv);
            }
        }
    }
    fclose(f);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing ISM-MEM-LIVE header");
        return -1;
    }
    if (open_stub(live, size, err, err_cap) != 0) return -1;
    snprintf(live->endpoint, sizeof live->endpoint, "%s", path);
    return 0;
}

int ib_memory_live_open(IbMemLive *live, const char *endpoint, int size_hint,
                        char *err, int err_cap) {
    if (!live || !endpoint) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    if (looks_like_live_cdc(endpoint)) {
        if (err && err_cap)
            snprintf(err, err_cap,
                     "live Soft MEM CDC (%s) deferred until Eric freezes "
                     "Soft/host path; use stub or fixtures/memory/*.sqmem",
                     endpoint);
        return -1;
    }
    if (strcmp(endpoint, "stub") == 0 || starts_with(endpoint, "stub:"))
        return open_stub(live, size_hint > 0 ? size_hint : 4096, err, err_cap);
    {
        const char *dot = strrchr(endpoint, '.');
        if (dot && strcmp(dot, ".sqmem") == 0)
            return open_sqmem(live, endpoint, size_hint, err, err_cap);
    }
    if (err && err_cap)
        snprintf(err, err_cap,
                 "unknown Soft live endpoint %s (use stub or .sqmem)", endpoint);
    return -1;
}

int ib_memory_live_close(IbMemLive *live) {
    if (!live) return -1;
    if (live->owned && live->data) free(live->data);
    memset(live, 0, sizeof *live);
    return 0;
}

int ib_memory_live_write(IbMemLive *live, int off, const unsigned char *src,
                         int n, char *err, int err_cap) {
    if (!live || !live->data || !src || n < 0 || off < 0 ||
        off > live->size || n > live->size - off) {
        if (err && err_cap) snprintf(err, err_cap, "live write range");
        return -1;
    }
    memcpy(live->data + off, src, (size_t)n);
    return 0;
}

int ib_memory_live_read(IbMemLive *live, int off, unsigned char *dst, int n,
                        char *err, int err_cap) {
    if (!live || !live->data || !dst || n < 0 || off < 0 ||
        off > live->size || n > live->size - off) {
        if (err && err_cap) snprintf(err, err_cap, "live read range");
        return -1;
    }
    memcpy(dst, live->data + off, (size_t)n);
    return 0;
}

static int live_const(IbMemLive *live, int n, unsigned char v,
                      IbMemTestResult *res) {
    unsigned char *tmp = malloc((size_t)n);
    int i;
    char err[64];
    if (!tmp) return fail_pat(res);
    for (i = 0; i < n; i++) tmp[i] = v;
    if (ib_memory_live_write(live, 0, tmp, n, err, sizeof err) != 0) {
        free(tmp);
        return fail_pat(res);
    }
    memset(tmp, 0x5a, (size_t)n);
    if (ib_memory_live_read(live, 0, tmp, n, err, sizeof err) != 0) {
        free(tmp);
        return fail_pat(res);
    }
    for (i = 0; i < n; i++) {
        if (tmp[i] != v) {
            free(tmp);
            return fail_pat(res);
        }
    }
    free(tmp);
    res->npass++;
    return 0;
}

static int live_walking(IbMemLive *live, int n, int ones, IbMemTestResult *res) {
    unsigned char *tmp = malloc((size_t)n);
    int bit, i;
    char err[64];
    if (!tmp) return fail_pat(res);
    for (bit = 0; bit < 8; bit++) {
        unsigned char mask = (unsigned char)(1u << bit);
        for (i = 0; i < n; i++) tmp[i] = ones ? mask : (unsigned char)~mask;
        if (ib_memory_live_write(live, 0, tmp, n, err, sizeof err) != 0) {
            free(tmp);
            return fail_pat(res);
        }
        memset(tmp, 0, (size_t)n);
        if (ib_memory_live_read(live, 0, tmp, n, err, sizeof err) != 0) {
            free(tmp);
            return fail_pat(res);
        }
        for (i = 0; i < n; i++) {
            unsigned char expect = ones ? mask : (unsigned char)~mask;
            if (tmp[i] != expect) {
                free(tmp);
                return fail_pat(res);
            }
        }
        res->npass++;
    }
    free(tmp);
    return 0;
}

static int live_checker(IbMemLive *live, int n, IbMemTestResult *res) {
    unsigned char *tmp = malloc((size_t)n);
    int i, pass;
    char err[64];
    if (!tmp) return fail_pat(res);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < n; i++) {
            if (pass == 0)
                tmp[i] = (i & 1) ? 0xAAu : 0x55u;
            else
                tmp[i] = (i & 1) ? 0x55u : 0xAAu;
        }
        if (ib_memory_live_write(live, 0, tmp, n, err, sizeof err) != 0) {
            free(tmp);
            return fail_pat(res);
        }
        memset(tmp, 0, (size_t)n);
        if (ib_memory_live_read(live, 0, tmp, n, err, sizeof err) != 0) {
            free(tmp);
            return fail_pat(res);
        }
        for (i = 0; i < n; i++) {
            unsigned char expect =
                (pass == 0) ? ((i & 1) ? 0xAAu : 0x55u) : ((i & 1) ? 0x55u : 0xAAu);
            if (tmp[i] != expect) {
                free(tmp);
                return fail_pat(res);
            }
        }
        res->npass++;
    }
    free(tmp);
    return 0;
}

static int live_lcg(IbMemLive *live, int n, unsigned int seed,
                    IbMemTestResult *res) {
    unsigned char *tmp = malloc((size_t)n);
    unsigned int s = seed ? seed : 1u;
    int i;
    char err[64];
    if (!tmp) return fail_pat(res);
    for (i = 0; i < n; i++) tmp[i] = ib_lcg_byte(&s);
    if (ib_memory_live_write(live, 0, tmp, n, err, sizeof err) != 0) {
        free(tmp);
        return fail_pat(res);
    }
    memset(tmp, 0, (size_t)n);
    if (ib_memory_live_read(live, 0, tmp, n, err, sizeof err) != 0) {
        free(tmp);
        return fail_pat(res);
    }
    s = seed ? seed : 1u;
    for (i = 0; i < n; i++) {
        unsigned char expect = ib_lcg_byte(&s);
        if (tmp[i] != expect) {
            free(tmp);
            return fail_pat(res);
        }
    }
    free(tmp);
    res->npass++;
    return 0;
}

int ib_memory_live_march(IbMemLive *live, const IbMemoryIface *m,
                         IbMemTestResult *res) {
    int n;
    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!live || !m || !live->data) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "live march: null → FAIL");
        return 0;
    }
    if (!ib_memory_spd_checksum_ok(m)) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary,
                 "live march part=%s spd_xor FAIL → FAIL", m->part);
        return 0;
    }
    res->npass++; /* SPD XOR */
    n = m->pattern_bytes;
    if (n > live->size) n = live->size;
    if (n < 16) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "live march: bank too small → FAIL");
        return 0;
    }
    if (live_const(live, n, 0x00, res) != 0) goto done;
    if (live_const(live, n, 0xff, res) != 0) goto done;
    if (live_walking(live, n, 1, res) != 0) goto done;
    if (live_walking(live, n, 0, res) != 0) goto done;
    if (live_checker(live, n, res) != 0) goto done;
    if (live_lcg(live, n, m->lcg_seed, res) != 0) goto done;
done:
    snprintf(res->summary, sizeof res->summary,
             "live march part=%s ep=%s size=%d patterns_ok=%d fail=%d "
             "(Soft host Soft-driven; NR Ch.7.1 LCG oracle) → %s",
             m->part, live->endpoint, n, res->npass, res->nfail,
             res->ok ? "PASS" : "FAIL");
    return 0;
}

#include "rm_live.h"
#include "rm_spd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int starts_with(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

static unsigned int rm_park_miller(unsigned int *state)
{
    const unsigned int a = 16807u;
    const unsigned int m = 2147483647u;
    unsigned long long prod =
        (unsigned long long)(*state) * (unsigned long long)a;
    *state = (unsigned int)(prod % m);
    if (*state == 0)
        *state = 1;
    return *state;
}

static unsigned char rm_lcg_byte(unsigned int *state)
{
    return (unsigned char)(rm_park_miller(state) & 0xffu);
}

static int fail_pat(RmTestResult *res)
{
    res->nfail++;
    res->ok = 0;
    return -1;
}

int rm_live_open(RmLive *live, const char *endpoint, int size_hint, char *err,
                 int err_cap)
{
    int size = size_hint > 0 ? size_hint : 4096;
    if (!live || !endpoint) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "null args");
        return -1;
    }
    if (starts_with(endpoint, "/dev/ttyACM") ||
        starts_with(endpoint, "/dev/ttyUSB")) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap,
                     "live Soft MEM CDC (%s) deferred; use stub", endpoint);
        return -1;
    }
    if (strcmp(endpoint, "stub") != 0 && !starts_with(endpoint, "stub:") &&
        !(strrchr(endpoint, '.') &&
          strcmp(strrchr(endpoint, '.'), ".sqmem") == 0)) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "unknown Soft live endpoint");
        return -1;
    }
    if (strrchr(endpoint, '.') &&
        strcmp(strrchr(endpoint, '.'), ".sqmem") == 0) {
        FILE *f = fopen(endpoint, "rb");
        char line[256];
        int saw = 0;
        if (!f) {
            if (err && err_cap)
                snprintf(err, (size_t)err_cap, "cannot open %s", endpoint);
            return -1;
        }
        while (fgets(line, (int)sizeof line, f)) {
            char *t = line;
            while (*t == ' ' || *t == '\t')
                t++;
            if (!*t || *t == '#')
                continue;
            if (!saw) {
                if (strncmp(t, "ISM-MEM-LIVE stub", 16) != 0) {
                    fclose(f);
                    if (err && err_cap)
                        snprintf(err, (size_t)err_cap,
                                 "need ISM-MEM-LIVE stub header");
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
                    while (*vk == ' ' || *vk == '\t')
                        vk++;
                    while (*vv == ' ' || *vv == '\t')
                        vv++;
                    if (strcmp(vk, "size") == 0)
                        size = atoi(vv);
                }
            }
        }
        fclose(f);
        if (!saw) {
            if (err && err_cap)
                snprintf(err, (size_t)err_cap, "missing header");
            return -1;
        }
    }
    if (size < 16 || size > RM_BANK_MAX) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "size out of range");
        return -1;
    }
    memset(live, 0, sizeof *live);
    live->data = calloc(1, (size_t)size);
    if (!live->data) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "oom");
        return -1;
    }
    live->size = size;
    live->owned = 1;
    snprintf(live->endpoint, sizeof live->endpoint, "%s", endpoint);
    return 0;
}

int rm_live_close(RmLive *live)
{
    if (!live)
        return -1;
    if (live->owned && live->data)
        free(live->data);
    memset(live, 0, sizeof *live);
    return 0;
}

static int live_write(RmLive *live, int off, const unsigned char *src, int n)
{
    if (!live || !live->data || !src || off < 0 || n < 0 ||
        off > live->size || n > live->size - off)
        return -1;
    memcpy(live->data + off, src, (size_t)n);
    return 0;
}

static int live_read(RmLive *live, int off, unsigned char *dst, int n)
{
    if (!live || !live->data || !dst || off < 0 || n < 0 ||
        off > live->size || n > live->size - off)
        return -1;
    memcpy(dst, live->data + off, (size_t)n);
    return 0;
}

static int live_const(RmLive *live, int n, unsigned char v, RmTestResult *res)
{
    unsigned char *tmp = malloc((size_t)n);
    int i;
    if (!tmp)
        return fail_pat(res);
    for (i = 0; i < n; i++)
        tmp[i] = v;
    if (live_write(live, 0, tmp, n) != 0) {
        free(tmp);
        return fail_pat(res);
    }
    memset(tmp, 0x5a, (size_t)n);
    if (live_read(live, 0, tmp, n) != 0) {
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

static int live_walking(RmLive *live, int n, int ones, RmTestResult *res)
{
    unsigned char *tmp = malloc((size_t)n);
    int bit, i;
    if (!tmp)
        return fail_pat(res);
    for (bit = 0; bit < 8; bit++) {
        unsigned char mask = (unsigned char)(1u << bit);
        for (i = 0; i < n; i++)
            tmp[i] = ones ? mask : (unsigned char)~mask;
        if (live_write(live, 0, tmp, n) != 0) {
            free(tmp);
            return fail_pat(res);
        }
        memset(tmp, 0, (size_t)n);
        if (live_read(live, 0, tmp, n) != 0) {
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

static int live_checker(RmLive *live, int n, RmTestResult *res)
{
    unsigned char *tmp = malloc((size_t)n);
    int i, pass;
    if (!tmp)
        return fail_pat(res);
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < n; i++)
            tmp[i] = (pass == 0) ? ((i & 1) ? 0xAAu : 0x55u)
                                 : ((i & 1) ? 0x55u : 0xAAu);
        if (live_write(live, 0, tmp, n) != 0) {
            free(tmp);
            return fail_pat(res);
        }
        memset(tmp, 0, (size_t)n);
        if (live_read(live, 0, tmp, n) != 0) {
            free(tmp);
            return fail_pat(res);
        }
        for (i = 0; i < n; i++) {
            unsigned char expect = (pass == 0) ? ((i & 1) ? 0xAAu : 0x55u)
                                               : ((i & 1) ? 0x55u : 0xAAu);
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

static int live_lcg(RmLive *live, int n, unsigned int seed, RmTestResult *res)
{
    unsigned char *tmp = malloc((size_t)n);
    unsigned int s = seed ? seed : 1u;
    int i;
    if (!tmp)
        return fail_pat(res);
    for (i = 0; i < n; i++)
        tmp[i] = rm_lcg_byte(&s);
    if (live_write(live, 0, tmp, n) != 0) {
        free(tmp);
        return fail_pat(res);
    }
    memset(tmp, 0, (size_t)n);
    if (live_read(live, 0, tmp, n) != 0) {
        free(tmp);
        return fail_pat(res);
    }
    s = seed ? seed : 1u;
    for (i = 0; i < n; i++) {
        if (tmp[i] != rm_lcg_byte(&s)) {
            free(tmp);
            return fail_pat(res);
        }
    }
    free(tmp);
    res->npass++;
    return 0;
}

int rm_live_march(RmLive *live, const RmIface *m, RmTestResult *res)
{
    int n;
    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!live || !m || !live->data) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "live march null → FAIL");
        return 0;
    }
    if (!rm_spd_checksum_ok(m)) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary,
                 "live march spd_xor FAIL → FAIL");
        return 0;
    }
    res->npass++;
    n = m->pattern_bytes;
    if (n > live->size)
        n = live->size;
    if (n < 16) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "live bank too small → FAIL");
        return 0;
    }
    if (live_const(live, n, 0x00, res) != 0)
        goto done;
    if (live_const(live, n, 0xff, res) != 0)
        goto done;
    if (live_walking(live, n, 1, res) != 0)
        goto done;
    if (live_walking(live, n, 0, res) != 0)
        goto done;
    if (live_checker(live, n, res) != 0)
        goto done;
    if (live_lcg(live, n, m->lcg_seed, res) != 0)
        goto done;
done:
    snprintf(res->summary, sizeof res->summary,
             "live march part=%s ep=%s size=%d patterns_ok=%d fail=%d → %s",
             m->part, live->endpoint, n, res->npass, res->nfail,
             res->ok ? "PASS" : "FAIL");
    return 0;
}

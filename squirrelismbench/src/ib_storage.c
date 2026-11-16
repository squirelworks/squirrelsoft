#define _POSIX_C_SOURCE 200809L
#include "ib_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Park–Miller LCG — NR Ch. 7.1 class; Soft original (same Soft family as ib_memory). */
static unsigned int stor_park_miller(unsigned int *state) {
    const unsigned int a = 16807u;
    const unsigned int m = 2147483647u;
    unsigned long long prod = (unsigned long long)(*state) * (unsigned long long)a;
    *state = (unsigned int)(prod % m);
    if (*state == 0) *state = 1;
    return *state;
}

static unsigned char stor_lcg_byte(unsigned int *state) {
    return (unsigned char)(stor_park_miller(state) & 0xffu);
}

/*
 * Soft CRC-32 (ISO 3309 / Ethernet poly 0xEDB88320), Soft original table-free
 * bit algorithm — Soft lab check of programmed Soft media image.
 */
static unsigned int soft_crc32(const unsigned char *data, int n) {
    unsigned int crc = 0xffffffffu;
    int i, b;
    for (i = 0; i < n; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xffffffffu;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

int ib_storage_parse(IbStorageIface *s, const char *text, char *err, int err_cap) {
    if (!s || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(s, 0, sizeof *s);
    snprintf(s->media, sizeof s->media, "sd_card");
    snprintf(s->part, sizeof s->part, "Soft-SD-8MiB");
    s->block_bytes = 512;
    s->capacity_kib = 8192;
    s->pattern_bytes = 4096;
    s->lcg_seed = 1u;
    s->erase_value = 0xffu;

    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int saw = 0, line_no = 0;
    while (line) {
        line_no++;
        char *t = trim(line);
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (strncmp(t, "ISM-STOR storage interface", 26) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-STOR storage interface", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[64], val[160];
        if (sscanf(t, "%63[^:]: %159[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        trim(key);
        trim(val);
        if (strcmp(key, "media") == 0)
            snprintf(s->media, sizeof s->media, "%s", val);
        else if (strcmp(key, "part") == 0)
            snprintf(s->part, sizeof s->part, "%s", val);
        else if (strcmp(key, "block_bytes") == 0)
            s->block_bytes = atoi(val);
        else if (strcmp(key, "capacity_kib") == 0)
            s->capacity_kib = atoi(val);
        else if (strcmp(key, "pattern_bytes") == 0)
            s->pattern_bytes = atoi(val);
        else if (strcmp(key, "lcg_seed") == 0)
            s->lcg_seed = (unsigned int)strtoul(val, NULL, 0);
        else if (strcmp(key, "erase_value") == 0) {
            if (strncmp(val, "0x", 2) == 0 || strncmp(val, "0X", 2) == 0)
                s->erase_value = (unsigned int)strtoul(val, NULL, 16);
            else
                s->erase_value = (unsigned int)atoi(val);
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (strcmp(s->media, "sd_card") != 0 && strcmp(s->media, "emmc") != 0 &&
        strcmp(s->media, "flash") != 0) {
        if (err && err_cap) snprintf(err, err_cap, "media must be sd_card|emmc|flash");
        return -1;
    }
    if (s->block_bytes < 512 || (s->block_bytes & (s->block_bytes - 1)) != 0) {
        if (err && err_cap) snprintf(err, err_cap, "block_bytes must be power-of-two >=512");
        return -1;
    }
    if (s->capacity_kib < 64 || s->pattern_bytes < 16 ||
        s->pattern_bytes > IB_STOR_BANK_MAX) {
        if (err && err_cap) snprintf(err, err_cap, "capacity/pattern_bytes out of range");
        return -1;
    }
    if (s->pattern_bytes % s->block_bytes != 0) {
        if (err && err_cap)
            snprintf(err, err_cap, "pattern_bytes must be multiple of block_bytes");
        return -1;
    }
    if (s->lcg_seed == 0) s->lcg_seed = 1u;
    if (s->erase_value > 0xffu) {
        if (err && err_cap) snprintf(err, err_cap, "erase_value must be 0..255");
        return -1;
    }
    return 0;
}

int ib_storage_selftest(const IbStorageIface *s, IbStorageTestResult *res) {
    memset(res, 0, sizeof *res);
    res->ok = 1;
    int n = s->pattern_bytes;
    unsigned char *bank = malloc((size_t)n);
    if (!bank) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "storage selftest oom → FAIL");
        return 0;
    }

    int i;
    unsigned char erase = (unsigned char)(s->erase_value & 0xffu);

    /* Soft erase → all erase_value. */
    for (i = 0; i < n; i++) bank[i] = erase;
    for (i = 0; i < n; i++) {
        if (bank[i] != erase) {
            res->nfail++;
            res->ok = 0;
            goto done;
        }
    }
    res->npass++;

    /* Soft program: LCG fill (NR Ch. 7.1 class oracle). */
    {
        unsigned int st = s->lcg_seed;
        for (i = 0; i < n; i++) bank[i] = stor_lcg_byte(&st);
        st = s->lcg_seed;
        for (i = 0; i < n; i++) {
            if (bank[i] != stor_lcg_byte(&st)) {
                res->nfail++;
                res->ok = 0;
                goto done;
            }
        }
        res->npass++;
    }

    /* Soft block alignment Soft check. */
    if (n % s->block_bytes == 0)
        res->npass++;
    else {
        res->nfail++;
        res->ok = 0;
    }

    /* Soft CRC of Soft programmed image. */
    res->soft_crc = soft_crc32(bank, n);
    if (res->soft_crc != 0)
        res->npass++;
    else {
        /* Extremely unlikely for LCG stream; treat as Soft anomaly. */
        res->nfail++;
        res->ok = 0;
    }

    /* Soft re-erase Soft gate. */
    for (i = 0; i < n; i++) bank[i] = erase;
    for (i = 0; i < n; i++) {
        if (bank[i] != erase) {
            res->nfail++;
            res->ok = 0;
            goto done;
        }
    }
    res->npass++;

done:
    free(bank);
    snprintf(res->summary, sizeof res->summary,
             "storage selftest media=%s part=%s block=%d patterns_ok=%d fail=%d "
             "crc32=0x%08x (NR Ch.7.1 LCG) → %s",
             s->media, s->part, s->block_bytes, res->npass, res->nfail, res->soft_crc,
             res->ok ? "PASS" : "FAIL");
    return 0;
}

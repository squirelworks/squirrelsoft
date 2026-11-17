#include "rm_spd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rm_spd_checksum_ok(const RmIface *m)
{
    int i;
    unsigned char x = 0;

    if (!m || m->spd_len < 2 || m->spd_len > RM_SPD_MAX)
        return 0;
    for (i = 0; i < m->spd_len - 1; i++)
        x ^= m->spd[i];
    return x == m->spd[m->spd_len - 1];
}

static int starts_with(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

int rm_spd_parse_line(const char *line, unsigned char *spd, int *spd_len,
                      int spd_cap, char *err, int err_cap)
{
    const char *p;
    int n = 0;

    if (!line || !spd || !spd_len || spd_cap < 2) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "null args");
        return -1;
    }
    p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r')
        p++;
    if (!starts_with(p, "ISM SPD:")) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "need ISM SPD: prefix");
        return -1;
    }
    p += 8;
    while (*p == ' ' || *p == '\t')
        p++;
    while (*p && *p != '\n' && *p != '\r') {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '\n' || *p == '\r')
            break;
        if (starts_with(p, "addr=") || starts_with(p, "ADDR=") ||
            starts_with(p, "len=") || starts_with(p, "LEN=")) {
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                p++;
            continue;
        }
        {
            char *end = NULL;
            unsigned long v = strtoul(p, &end, 16);
            if (end == p) {
                if (err && err_cap)
                    snprintf(err, (size_t)err_cap, "bad SPD hex");
                return -1;
            }
            if (n >= spd_cap) {
                if (err && err_cap)
                    snprintf(err, (size_t)err_cap, "SPD too long");
                return -1;
            }
            if (v > 0xfful) {
                if (err && err_cap)
                    snprintf(err, (size_t)err_cap, "SPD byte out of range");
                return -1;
            }
            spd[n++] = (unsigned char)(v & 0xffu);
            p = end;
        }
    }
    if (n < 2) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "SPD needs >= 2 bytes");
        return -1;
    }
    *spd_len = n;
    return 0;
}

int rm_spd_apply_cdc_path(RmIface *m, const char *path, char *err, int err_cap)
{
    FILE *f;
    char line[512];
    unsigned char tmp[RM_SPD_MAX];
    int len = 0, found = 0, i;

    if (!m || !path) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "null args");
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "cannot open %s", path);
        return -1;
    }
    for (i = 0; i < 64 && fgets(line, (int)sizeof line, f); i++) {
        char *t = line;
        while (*t == ' ' || *t == '\t')
            t++;
        if (!starts_with(t, "ISM SPD:"))
            continue;
        if (rm_spd_parse_line(t, tmp, &len, RM_SPD_MAX, err, err_cap) != 0) {
            fclose(f);
            return -1;
        }
        found = 1;
        break;
    }
    fclose(f);
    if (!found) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "no ISM SPD: line in %s", path);
        return -1;
    }
    memcpy(m->spd, tmp, (size_t)len);
    m->spd_len = len;
    if (!rm_spd_checksum_ok(m)) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "SPD Soft XOR contract failed");
        return -1;
    }
    return 0;
}

int rm_spd_print(const RmIface *m, char *buf, int buflen)
{
    int i, n = 0;

    if (!m || !buf || buflen < 32)
        return -1;
    n += snprintf(buf + n, (size_t)(buflen - n),
                  "part=%s density_kib=%d banks=%d dq=%d\n", m->part,
                  m->density_kib, m->banks, m->dq_width);
    if (n < 0 || n >= buflen)
        return -1;
    n += snprintf(buf + n, (size_t)(buflen - n),
                  "spd_i2c=0x%02x spd_len=%d xor=%s pattern_bytes=%d\n",
                  m->spd_i2c_addr, m->spd_len,
                  rm_spd_checksum_ok(m) ? "ok" : "FAIL", m->pattern_bytes);
    if (n < 0 || n >= buflen)
        return -1;
    n += snprintf(buf + n, (size_t)(buflen - n), "spd:");
    if (n < 0 || n >= buflen)
        return -1;
    for (i = 0; i < m->spd_len; i++) {
        n += snprintf(buf + n, (size_t)(buflen - n), " %02x", m->spd[i]);
        if (n < 0 || n >= buflen)
            return -1;
    }
    n += snprintf(buf + n, (size_t)(buflen - n), "\n");
    return (n < 0 || n >= buflen) ? -1 : 0;
}

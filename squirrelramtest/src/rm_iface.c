#include "rm_iface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rm_iface_parse(RmIface *m, const char *text, char *err, int err_cap)
{
    char *buf, *save = NULL, *line;
    int saw = 0, line_no = 0, spd_set = 0;

    if (!m || !text) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "null args");
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

    buf = strdup(text);
    if (!buf) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "oom");
        return -1;
    }
    line = strtok_r(buf, "\n", &save);
    while (line) {
        char *t = line;
        char *e;
        char key[64], val[400];
        char *vk, *vv;

        line_no++;
        while (*t == ' ' || *t == '\t')
            t++;
        e = t + strlen(t);
        while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            *--e = 0;
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (strncmp(t, "ISM-MEM memory interface", 24) != 0) {
                if (err && err_cap)
                    snprintf(err, (size_t)err_cap,
                             "line %d: need ISM-MEM memory interface", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (sscanf(t, "%63[^:]: %399[^\n]", key, val) != 2) {
            if (err && err_cap)
                snprintf(err, (size_t)err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        vk = key;
        while (*vk == ' ' || *vk == '\t')
            vk++;
        vv = val;
        while (*vv == ' ' || *vv == '\t')
            vv++;
        if (strcmp(vk, "part") == 0)
            snprintf(m->part, sizeof m->part, "%s", vv);
        else if (strcmp(vk, "density_kib") == 0)
            m->density_kib = atoi(vv);
        else if (strcmp(vk, "banks") == 0)
            m->banks = atoi(vv);
        else if (strcmp(vk, "spd_i2c_addr") == 0) {
            if (strncmp(vv, "0x", 2) == 0 || strncmp(vv, "0X", 2) == 0)
                m->spd_i2c_addr = (int)strtol(vv, NULL, 16);
            else
                m->spd_i2c_addr = atoi(vv);
        } else if (strcmp(vk, "spd_bytes") == 0)
            m->spd_len = atoi(vv);
        else if (strcmp(vk, "dq_width") == 0)
            m->dq_width = atoi(vv);
        else if (strcmp(vk, "pattern_bytes") == 0)
            m->pattern_bytes = atoi(vv);
        else if (strcmp(vk, "lcg_seed") == 0)
            m->lcg_seed = (unsigned int)strtoul(vv, NULL, 0);
        else if (strcmp(vk, "spd") == 0) {
            int n = 0;
            const char *p = vv;
            while (*p && n < RM_SPD_MAX) {
                char *end = NULL;
                unsigned long v;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (!*p)
                    break;
                v = strtoul(p, &end, 16);
                if (end == p) {
                    if (err && err_cap)
                        snprintf(err, (size_t)err_cap, "line %d: bad spd hex",
                                 line_no);
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
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "missing header");
        return -1;
    }
    if (!spd_set || m->spd_len < 2 || m->spd_len > RM_SPD_MAX) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "spd required (2..%d bytes)", RM_SPD_MAX);
        return -1;
    }
    if (m->banks < 1 || m->banks > 8 || m->density_kib <= 0 || m->dq_width <= 0) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "bad memory geometry");
        return -1;
    }
    if (m->pattern_bytes < 16 || m->pattern_bytes > RM_BANK_MAX) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "pattern_bytes out of range");
        return -1;
    }
    if (m->spd_i2c_addr != 0x50) {
        if (err && err_cap)
            snprintf(err, (size_t)err_cap, "spd_i2c_addr must be 0x50 (ISM J2/edge)");
        return -1;
    }
    if (m->lcg_seed == 0)
        m->lcg_seed = 1u;
    return 0;
}

int rm_iface_load(const char *path, RmIface *out, char *err, int errlen)
{
    FILE *f;
    long n;
    char *text;
    int rc;

    if (!path || !out) {
        if (err && errlen)
            snprintf(err, (size_t)errlen, "null args");
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        if (err && errlen)
            snprintf(err, (size_t)errlen, "open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (err && errlen)
            snprintf(err, (size_t)errlen, "seek %s", path);
        return -1;
    }
    n = ftell(f);
    if (n < 0 || n > 1024 * 1024) {
        fclose(f);
        if (err && errlen)
            snprintf(err, (size_t)errlen, "size %s", path);
        return -1;
    }
    rewind(f);
    text = malloc((size_t)n + 1u);
    if (!text) {
        fclose(f);
        if (err && errlen)
            snprintf(err, (size_t)errlen, "oom");
        return -1;
    }
    if (fread(text, 1, (size_t)n, f) != (size_t)n) {
        free(text);
        fclose(f);
        if (err && errlen)
            snprintf(err, (size_t)errlen, "read %s", path);
        return -1;
    }
    text[n] = 0;
    fclose(f);
    rc = rm_iface_parse(out, text, err, errlen);
    free(text);
    if (rc == 0)
        snprintf(out->path, sizeof out->path, "%s", path);
    return rc;
}

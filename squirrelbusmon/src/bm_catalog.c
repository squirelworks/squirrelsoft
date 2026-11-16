#define _POSIX_C_SOURCE 200809L
#include "bm_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_msgid(const char *s, unsigned *out) {
    unsigned long v;
    char *end = NULL;
    if (!s || !out) return -1;
    v = strtoul(s, &end, 0);
    if (!end || end == s || *end) return -1;
    *out = (unsigned)v;
    return 0;
}

int bm_catalog_parse(BmCatalog *cat, const char *text, char *err, int err_cap) {
    char *buf, *save = NULL, *line;
    int saw = 0, line_no = 0;

    if (!cat || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(cat, 0, sizeof *cat);
    buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    line = strtok_r(buf, "\n", &save);
    while (line) {
        char *t;
        line_no++;
        t = bm_trim(line);
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (!bm_starts_with(t, "ISM-BUS-ICD ")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-BUS-ICD header", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (bm_starts_with(t, "aircraft:")) {
            snprintf(cat->aircraft, sizeof cat->aircraft, "%s", bm_trim(t + 9));
        } else if (bm_starts_with(t, "revision:")) {
            snprintf(cat->revision, sizeof cat->revision, "%s", bm_trim(t + 9));
        } else if (bm_starts_with(t, "source:")) {
            snprintf(cat->source, sizeof cat->source, "%s", bm_trim(t + 7));
        } else if (bm_starts_with(t, "topic ")) {
            char keys[8][BM_NAME_LEN], vals[8][BM_FIELD_LEN];
            char name[BM_NAME_LEN];
            const char *rest;
            int nk, i;
            BmTopic *tp;
            rest = t + 6;
            while (*rest == ' ') rest++;
            {
                const char *sp = strchr(rest, ' ');
                size_t nlen;
                if (!sp) {
                    if (err && err_cap) snprintf(err, err_cap, "line %d: topic needs kv", line_no);
                    free(buf);
                    return -1;
                }
                nlen = (size_t)(sp - rest);
                if (nlen >= sizeof name) nlen = sizeof name - 1;
                memcpy(name, rest, nlen);
                name[nlen] = 0;
                rest = sp + 1;
            }
            nk = bm_parse_kv(rest, keys, vals, 8);
            if (nk < 0) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: bad topic kv", line_no);
                free(buf);
                return -1;
            }
            if (cat->ntopics >= BM_MAX_TOPICS) {
                if (err && err_cap) snprintf(err, err_cap, "too many topics");
                free(buf);
                return -1;
            }
            tp = &cat->topics[cat->ntopics];
            memset(tp, 0, sizeof *tp);
            snprintf(tp->name, sizeof tp->name, "%s", name);
            for (i = 0; i < nk; i++) {
                if (strcmp(keys[i], "bus") == 0)
                    snprintf(tp->bus, sizeof tp->bus, "%s", vals[i]);
                else if (strcmp(keys[i], "msgid") == 0) {
                    if (parse_msgid(vals[i], &tp->msgid) != 0) {
                        if (err && err_cap)
                            snprintf(err, err_cap, "line %d: bad msgid", line_no);
                        free(buf);
                        return -1;
                    }
                } else if (strcmp(keys[i], "rate_hz") == 0)
                    tp->rate_hz = atoi(vals[i]);
                else if (strcmp(keys[i], "payload") == 0)
                    snprintf(tp->payload, sizeof tp->payload, "%s", vals[i]);
                else if (strcmp(keys[i], "opaque") == 0)
                    tp->opaque = atoi(vals[i]) ? 1 : 0;
            }
            if (!tp->bus[0] || !tp->msgid) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: topic needs bus and msgid", line_no);
                free(buf);
                return -1;
            }
            cat->ntopics++;
        } else {
            if (err && err_cap) snprintf(err, err_cap, "line %d: unknown catalog line", line_no);
            free(buf);
            return -1;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (cat->ntopics < 1) {
        if (err && err_cap) snprintf(err, err_cap, "no topics");
        return -1;
    }
    return 0;
}

const BmTopic *bm_catalog_find_topic(const BmCatalog *cat, const char *name) {
    int i;
    if (!cat || !name) return NULL;
    for (i = 0; i < cat->ntopics; i++) {
        if (strcmp(cat->topics[i].name, name) == 0) return &cat->topics[i];
    }
    return NULL;
}

const BmTopic *bm_catalog_find_msgid(const BmCatalog *cat, unsigned msgid) {
    int i;
    if (!cat) return NULL;
    for (i = 0; i < cat->ntopics; i++) {
        if (cat->topics[i].msgid == msgid) return &cat->topics[i];
    }
    return NULL;
}

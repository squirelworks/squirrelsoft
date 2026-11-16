#define _POSIX_C_SOURCE 200809L
#include "bm_frame.h"

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

/* Safe display keys allowed even on opaque SMS/stores frames. */
static int field_is_safe_display(const char *key) {
    static const char *const ok[] = {
        "mode", "state", "stations", "inhibit", "reason", "station_id", NULL};
    int i;
    for (i = 0; ok[i]; i++) {
        if (strcmp(key, ok[i]) == 0) return 1;
    }
    return 0;
}

int bm_capture_parse(BmCapture *cap, const char *text, char *err, int err_cap) {
    char *buf, *save = NULL, *line;
    int saw = 0, line_no = 0;

    if (!cap || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(cap, 0, sizeof *cap);
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
            if (!bm_starts_with(t, "ISM-BUS-CAPTURE ")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-BUS-CAPTURE header", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (bm_starts_with(t, "frame ")) {
            char keys[BM_MAX_FIELDS + 8][BM_NAME_LEN];
            char vals[BM_MAX_FIELDS + 8][BM_FIELD_LEN];
            int nk, i;
            BmFrame *fr;
            nk = bm_parse_kv(t + 6, keys, vals, BM_MAX_FIELDS + 8);
            if (nk < 0) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: bad frame", line_no);
                free(buf);
                return -1;
            }
            if (cap->nframes >= BM_MAX_FRAMES) {
                if (err && err_cap) snprintf(err, err_cap, "too many frames");
                free(buf);
                return -1;
            }
            fr = &cap->frames[cap->nframes];
            memset(fr, 0, sizeof *fr);
            for (i = 0; i < nk; i++) {
                if (strcmp(keys[i], "t_ms") == 0)
                    fr->t_ms = atol(vals[i]);
                else if (strcmp(keys[i], "bus") == 0)
                    snprintf(fr->bus, sizeof fr->bus, "%s", vals[i]);
                else if (strcmp(keys[i], "node") == 0)
                    snprintf(fr->node, sizeof fr->node, "%s", vals[i]);
                else if (strcmp(keys[i], "msgid") == 0) {
                    if (parse_msgid(vals[i], &fr->msgid) != 0) {
                        if (err && err_cap)
                            snprintf(err, err_cap, "line %d: bad msgid", line_no);
                        free(buf);
                        return -1;
                    }
                } else if (strcmp(keys[i], "topic") == 0)
                    snprintf(fr->topic, sizeof fr->topic, "%s", vals[i]);
                else {
                    if (fr->nfields < BM_MAX_FIELDS) {
                        snprintf(fr->fields[fr->nfields].key,
                                 sizeof fr->fields[0].key, "%s", keys[i]);
                        snprintf(fr->fields[fr->nfields].val,
                                 sizeof fr->fields[0].val, "%s", vals[i]);
                        fr->nfields++;
                    }
                }
            }
            if (!fr->bus[0] || !fr->node[0] || !fr->topic[0] || !fr->msgid) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: frame needs bus/node/msgid/topic",
                             line_no);
                free(buf);
                return -1;
            }
            cap->nframes++;
        } else {
            if (err && err_cap) snprintf(err, err_cap, "line %d: unknown capture line", line_no);
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
    if (cap->nframes < 1) {
        if (err && err_cap) snprintf(err, err_cap, "no frames");
        return -1;
    }
    return 0;
}

int bm_capture_decode(BmCapture *cap, const BmCatalog *cat, char *err, int err_cap) {
    int i;
    if (!cap || !cat) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    for (i = 0; i < cap->nframes; i++) {
        BmFrame *fr = &cap->frames[i];
        const BmTopic *tp = bm_catalog_find_topic(cat, fr->topic);
        char body[BM_LINE_LEN];
        int used = 0, j;

        if (!tp) tp = bm_catalog_find_msgid(cat, fr->msgid);
        if (!tp) {
            fr->known = 0;
            fr->opaque = 0;
            snprintf(fr->decode, sizeof fr->decode,
                     "UNKNOWN topic=%s msgid=0x%04X (not in ICD catalog)", fr->topic,
                     fr->msgid);
            continue;
        }
        fr->known = 1;
        fr->opaque = tp->opaque;
        if (tp->msgid != fr->msgid) {
            snprintf(fr->decode, sizeof fr->decode,
                     "MISMATCH topic=%s catalog_msgid=0x%04X frame_msgid=0x%04X", tp->name,
                     tp->msgid, fr->msgid);
            continue;
        }
        body[0] = 0;
        if (fr->opaque) {
            /* Weapons boundary: topic + safe display fields only. */
            used = snprintf(body, sizeof body, "OPAQUE topic=%s payload=%s", tp->name,
                            tp->payload);
            for (j = 0; j < fr->nfields; j++) {
                int w;
                if (!field_is_safe_display(fr->fields[j].key)) continue;
                if (strcmp(fr->fields[j].key, "stations") == 0 &&
                    strcmp(fr->fields[j].val, "opaque") != 0) {
                    w = snprintf(body + used, (size_t)(sizeof body - used),
                                 " stations=<opaque>");
                } else {
                    w = snprintf(body + used, (size_t)(sizeof body - used), " %s=%s",
                                 fr->fields[j].key, fr->fields[j].val);
                }
                if (w < 0 || used + w >= (int)sizeof body) break;
                used += w;
            }
        } else {
            used = snprintf(body, sizeof body, "payload=%s", tp->payload);
            for (j = 0; j < fr->nfields; j++) {
                int w = snprintf(body + used, (size_t)(sizeof body - used), " %s=%s",
                                 fr->fields[j].key, fr->fields[j].val);
                if (w < 0 || used + w >= (int)sizeof body) break;
                used += w;
            }
        }
        snprintf(fr->decode, sizeof fr->decode, "%s", body);
    }
    return 0;
}

int bm_frame_format(const BmFrame *fr, char *out, int out_cap) {
    if (!fr || !out || out_cap < 32) return -1;
    return snprintf(out, out_cap, "t_ms=%ld bus=%s node=%s msgid=0x%04X topic=%s %s",
                    fr->t_ms, fr->bus, fr->node, fr->msgid, fr->topic, fr->decode);
}

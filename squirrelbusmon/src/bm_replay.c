#define _POSIX_C_SOURCE 200809L
#include "bm_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bm_replay_build(const BmCapture *cap, char **out) {
    size_t cap_bytes = 256;
    size_t used = 0;
    char *buf;
    int i;

    if (!cap || !out) return -1;
    *out = NULL;
    buf = malloc(cap_bytes);
    if (!buf) return -1;
    {
        int w = snprintf(buf, cap_bytes,
                         "REPLAY v1\n"
                         "# Soft squirrelbusmon → Flight Lab / HIL ingest\n"
                         "# See squirrelbusmon/docs/replay-hook.md\n");
        if (w < 0) {
            free(buf);
            return -1;
        }
        used = (size_t)w;
    }
    for (i = 0; i < cap->nframes; i++) {
        const BmFrame *fr = &cap->frames[i];
        char line[BM_LINE_LEN + 64];
        int w;
        size_t need;
        w = snprintf(line, sizeof line,
                     "inject t_ms=%ld bus=%s node=%s msgid=0x%04X topic=%s display=%s\n",
                     fr->t_ms, fr->bus, fr->node, fr->msgid, fr->topic,
                     fr->decode[0] ? fr->decode : "-");
        if (w < 0) {
            free(buf);
            return -1;
        }
        need = used + (size_t)w + 1;
        if (need > cap_bytes) {
            size_t ncap = cap_bytes * 2;
            char *nb;
            while (ncap < need) ncap *= 2;
            nb = realloc(buf, ncap);
            if (!nb) {
                free(buf);
                return -1;
            }
            buf = nb;
            cap_bytes = ncap;
        }
        memcpy(buf + used, line, (size_t)w);
        used += (size_t)w;
        buf[used] = 0;
    }
    *out = buf;
    return 0;
}

#include "bm_filter.h"

#include <string.h>

int bm_filter_match(const BmFilter *f, const BmFrame *fr) {
    if (!f || !fr) return 0;
    if (f->node && f->node[0] && strcmp(fr->node, f->node) != 0) return 0;
    if (f->topic && f->topic[0] && strcmp(fr->topic, f->topic) != 0) return 0;
    if (f->bus && f->bus[0] && strcmp(fr->bus, f->bus) != 0) return 0;
    if (f->have_msgid && fr->msgid != f->msgid) return 0;
    return 1;
}

int bm_filter_apply(const BmFilter *f, const BmCapture *src, BmCapture *dst) {
    int i, n = 0;
    if (!f || !src || !dst) return -1;
    memset(dst, 0, sizeof *dst);
    for (i = 0; i < src->nframes; i++) {
        if (!bm_filter_match(f, &src->frames[i])) continue;
        if (n >= BM_MAX_FRAMES) break;
        dst->frames[n++] = src->frames[i];
    }
    dst->nframes = n;
    return n;
}

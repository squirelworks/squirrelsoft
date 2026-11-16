/*
 * Filter decoded frames by node / message ID / topic / bus.
 */
#ifndef BM_FILTER_H
#define BM_FILTER_H

#include "bm_frame.h"

typedef struct {
    const char *node;   /* NULL = any */
    const char *topic;  /* NULL = any */
    const char *bus;    /* NULL = any */
    int have_msgid;
    unsigned msgid;
} BmFilter;

/* Returns 1 if frame matches filter. */
int bm_filter_match(const BmFilter *f, const BmFrame *fr);

/* Copy matching frames into dst (clears dst first). Returns count. */
int bm_filter_apply(const BmFilter *f, const BmCapture *src, BmCapture *dst);

#endif

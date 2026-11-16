/*
 * Bus capture frames: import + decode against an ICD catalog.
 */
#ifndef BM_FRAME_H
#define BM_FRAME_H

#include "bm_catalog.h"

typedef struct {
    char key[BM_NAME_LEN];
    char val[BM_FIELD_LEN];
} BmField;

typedef struct {
    long t_ms;
    char bus[BM_NAME_LEN];
    char node[BM_NAME_LEN];
    unsigned msgid;
    char topic[BM_NAME_LEN];
    BmField fields[BM_MAX_FIELDS];
    int nfields;
    /* Filled by decode */
    int known;          /* matched catalog topic */
    int opaque;         /* catalog marks SMS/stores opaque */
    char decode[BM_LINE_LEN];
} BmFrame;

typedef struct {
    BmFrame frames[BM_MAX_FRAMES];
    int nframes;
} BmCapture;

int bm_capture_parse(BmCapture *cap, const char *text, char *err, int err_cap);
/* Decode every frame against catalog; writes frame->decode and flags. */
int bm_capture_decode(BmCapture *cap, const BmCatalog *cat, char *err, int err_cap);
/* Format one decoded frame line into out. Returns bytes written or -1. */
int bm_frame_format(const BmFrame *fr, char *out, int out_cap);

#endif

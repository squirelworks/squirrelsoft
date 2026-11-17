/*
 * Soft #204 — Soft host path for live PSRAM march / bank patterns.
 *
 * Soft drives write/read through this Soft bank API. CI uses the Soft
 * "stub" endpoint (in-memory Soft bank). Real ACM Soft MEM framing is
 * documented in docs/psram-live.md; opening /dev/ttyACM* is refused until
 * Eric freezes Soft/host SoT (same stand-down pattern as Soft HID).
 */
#ifndef IB_MEM_LIVE_H
#define IB_MEM_LIVE_H

#include "ib_memory.h"

typedef struct {
    unsigned char *data;
    int size;
    int owned; /* Soft malloc'd → Soft free on close */
    char endpoint[IB_NAME_LEN];
} IbMemLive;

/*
 * endpoint:
 *   "stub" / "stub:"*     → Soft in-memory bank (CI; size from iface/pattern)
 *   path ending .sqmem    → Soft load Soft live-stub descriptor (size=)
 *   /dev/ttyACM*|USB*     → refuse (live Soft MEM CDC deferred)
 * size_hint used when stub has no descriptor (typically m->pattern_bytes).
 */
int ib_memory_live_open(IbMemLive *live, const char *endpoint, int size_hint,
                        char *err, int err_cap);
int ib_memory_live_close(IbMemLive *live);

int ib_memory_live_write(IbMemLive *live, int off, const unsigned char *src,
                         int n, char *err, int err_cap);
int ib_memory_live_read(IbMemLive *live, int off, unsigned char *dst, int n,
                        char *err, int err_cap);

/*
 * Soft-driven march / walking / checker / Park–Miller LCG through Soft
 * live write/read (not a local malloc bank). SPD Soft XOR still checked
 * on m first. CI: open stub first.
 */
int ib_memory_live_march(IbMemLive *live, const IbMemoryIface *m,
                         IbMemTestResult *res);

#endif

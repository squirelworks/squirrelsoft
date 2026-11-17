#ifndef RM_LIVE_H
#define RM_LIVE_H

#include "rm_iface.h"
#include "rm_selftest.h"

typedef struct {
    unsigned char *data;
    int size;
    int owned;
    char endpoint[RM_NAME_LEN];
} RmLive;

int rm_live_open(RmLive *live, const char *endpoint, int size_hint, char *err,
                 int err_cap);
int rm_live_close(RmLive *live);
int rm_live_march(RmLive *live, const RmIface *m, RmTestResult *res);

#endif

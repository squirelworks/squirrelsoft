/* Mission-computer stub role. Not flight software. */
#ifndef MC_STUB_H
#define MC_STUB_H

#include "mc_catalog.h"

#define MC_STUB_MSG 192

typedef struct {
    int ok;
    char summary[MC_STUB_MSG];
} McStubTickResult;

/* Publish synthetic MC topics for one deterministic tick. */
void mc_stub_tick(const McCatalog *cat, unsigned tick, McStubTickResult *res);

#endif

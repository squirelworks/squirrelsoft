#include "mc_stub.h"

#include <stdio.h>
#include <string.h>

void mc_stub_tick(const McCatalog *cat, unsigned tick, McStubTickResult *res) {
    int a, b, c;

    if (!res) return;
    memset(res, 0, sizeof *res);
    if (!cat) {
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "MC tick %u FAIL: no catalog", tick);
        return;
    }
    a = mc_catalog_find(cat, "mc.stores_config");
    b = mc_catalog_find(cat, "mc.radar_tracks");
    c = mc_catalog_find(cat, "mc.mode_request");
    if (a < 0 || b < 0 || c < 0) {
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary,
                 "MC tick %u FAIL: missing mc.* topics", tick);
        return;
    }
    res->ok = 1;
    snprintf(res->summary, sizeof res->summary,
             "MC tick %u PASS pub mc.stores_config=%u mc.radar_tracks=%u mc.mode_request=%u",
             tick, (tick % 10) + 1u, (tick % 5) + 1u, (tick % 3) + 1u);
}

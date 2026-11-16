#include "mc_display.h"

#include <stdio.h>
#include <string.h>

void mc_display_tick(const McCatalog *cat, const McSmsDisplay *sms, unsigned tick,
                     McDisplayTickResult *res) {
    int i, nsel = 0;

    if (!res) return;
    memset(res, 0, sizeof *res);
    if (!cat || !sms) {
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "DISP tick %u FAIL: null", tick);
        return;
    }
    if (mc_catalog_find(cat, "mc.stores_config") < 0 ||
        mc_catalog_find(cat, "sms.safe_arm_discrete") < 0) {
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary,
                 "DISP tick %u FAIL: catalog missing display topics", tick);
        return;
    }
    for (i = 0; i < sms->nstations; i++) {
        if (strcmp(sms->stations[i].flag, "selected") == 0) nsel++;
    }
    res->ok = 1;
    snprintf(res->summary, sizeof res->summary,
             "DISP tick %u PASS page=stores_safeing selected=%d safe_arm=%s pins=%d",
             tick, nsel, sms->safeing.safe_arm_discrete,
             sms->safeing.mechanical_ground_pins);
}

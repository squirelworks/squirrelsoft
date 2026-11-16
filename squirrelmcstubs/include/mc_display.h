/* Display-page publisher stub (one page). Not flight software. */
#ifndef MC_DISPLAY_H
#define MC_DISPLAY_H

#include "mc_catalog.h"
#include "mc_sms.h"

#define MC_DISP_MSG 256

typedef struct {
    int ok;
    char summary[MC_DISP_MSG];
} McDisplayTickResult;

/* Publish one stores/safeing display page from SMS display state. */
void mc_display_tick(const McCatalog *cat, const McSmsDisplay *sms, unsigned tick,
                     McDisplayTickResult *res);

#endif

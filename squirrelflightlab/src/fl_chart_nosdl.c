/* Soft S7.4′ stub when SDL2 is not available at build time. */
#include "fl_chart_gui.h"

#include <stdio.h>

int fl_chart_gui_run(const FlProject *p, const FlChartStyle *st, double duration_s,
                     double dt, int max_frames) {
    (void)p;
    (void)st;
    (void)duration_s;
    (void)dt;
    (void)max_frames;
    fprintf(stderr,
            "squirrelflightlab: built without SDL2 — strip-chart --gui unavailable "
            "(headless --ppm still works)\n");
    return 2;
}

int fl_chart_gui_run_hil(const char *endpoint, const char *role0, const char *role1,
                         const FlChartStyle *st, int max_frames, char *err, int err_cap) {
    (void)endpoint;
    (void)role0;
    (void)role1;
    (void)st;
    (void)max_frames;
    if (err && err_cap)
        snprintf(err, err_cap, "built without SDL2 — hil-chart --gui unavailable");
    return 2;
}

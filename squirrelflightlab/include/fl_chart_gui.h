/*
 * Soft S7.4′ — optional SDL strip-chart window on the same fl_chart core.
 * Built with sqgui_sdl when pkg-config finds SDL2; otherwise a clear stub.
 */
#ifndef FL_CHART_GUI_H
#define FL_CHART_GUI_H

#include "fl_chart.h"
#include "fl_project.h"

/* Open an SDL window, draw Soft sim strip each frame. max_frames<=0 runs until
 * close/Escape; CI uses SDL_VIDEODRIVER=dummy + max_frames>0. Returns 0 ok. */
int fl_chart_gui_run(const FlProject *p, const FlChartStyle *st, double duration_s,
                     double dt, int max_frames);

/* Soft HIL strip in an SDL window (S7.5′ + S7.4′). */
int fl_chart_gui_run_hil(const char *endpoint, const char *role0, const char *role1,
                         const FlChartStyle *st, int max_frames, char *err, int err_cap);

#endif

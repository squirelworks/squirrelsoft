/*
 * Soft Flight Lab strip-chart on libs/sqgui (S7.4 + S7.4′ SDL + S7.5′ HIL).
 * Headless canvas draw + optional PPM; SDL window via fl_chart_gui when linked.
 */
#ifndef FL_CHART_H
#define FL_CHART_H

#include "fl_dyn.h"
#include "fl_hil.h"
#include "sqgui.h"

typedef enum {
    FL_CHART_V = 0,
    FL_CHART_NZ,
    FL_CHART_ALPHA,
    FL_CHART_THETA,
    FL_CHART_COUNT
} FlChartChannel;

typedef struct {
    FlChartChannel ch[4];
    int nch; /* 1..4 */
    int w, h;
    uint32_t bg, grid, axis, trace[4];
} FlChartStyle;

void fl_chart_style_default(FlChartStyle *st);

/*
 * Draw strip chart of sim history into canvas rect.
 * Resamples each series onto pixel columns with sqn_lerp (NR Ch. 3.1).
 * Returns number of polyline segments drawn, or -1 on error.
 */
int fl_chart_draw(sqg_canvas *c, sqg_rect r, const FlSim *sim, const FlChartStyle *st);

/* Run a short default sim and draw; used by CLI / tests. */
int fl_chart_render_demo(sqg_canvas *c, const FlProject *p, const FlChartStyle *st,
                         double duration_s, double dt);

/*
 * Soft S7.5′: draw Soft HIL board samples for `role` into the chart
 * (NR Ch. 3.1 lerp onto columns). Optional second role overlays trace[1].
 * Returns segments drawn, or -1 on error.
 */
int fl_chart_draw_hil(sqg_canvas *c, sqg_rect r, const FlHilSession *sess,
                      const char *role0, const char *role1, const FlChartStyle *st);

/* Connect Soft HIL endpoint, draw roles into canvas (CLI / tests). */
int fl_chart_render_hil(sqg_canvas *c, const char *endpoint, const char *role0,
                        const char *role1, const FlChartStyle *st, char *err,
                        int err_cap);

#endif

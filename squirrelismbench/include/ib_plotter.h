#ifndef IB_PLOTTER_H
#define IB_PLOTTER_H

#include "ib_iface.h"

#define IB_PLT_MAX_WP 32

typedef struct {
    char status[32]; /* soft_proposed / fab_ready */
    int axes;
    double x_steps_per_mm;
    double y_steps_per_mm;
    double workspace_x_mm;
    double workspace_y_mm;
    int nwp;
    double wp_x[IB_PLT_MAX_WP];
    double wp_y[IB_PLT_MAX_WP];
} IbPlotterIface;

typedef struct {
    int ok;
    int npass;
    int nfail;
    double path_mm;
    char summary[IB_MSG_LEN];
} IbPlotterTestResult;

int ib_plotter_parse(IbPlotterIface *p, const char *text, char *err, int err_cap);

/*
 * Soft plotter self-test: workspace clamp + polyline length via NR Ch. 3.1
 * linear interpolation samples between waypoints (squirrelnum sqn_lerp).
 */
int ib_plotter_selftest(const IbPlotterIface *p, IbPlotterTestResult *res);

#endif

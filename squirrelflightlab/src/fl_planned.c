#include "fl_planned.h"

#include <stdio.h>

static const FlPlannedSurface k_planned[] = {
    {FL_PLANNED_CFD_MESH, "cfd_mesh_import", "planned",
     "Import volume/surface meshes for higher-fidelity aero correlation. Not wired in v0."},
    {FL_PLANNED_WIND_TUNNEL, "wind_tunnel_correlate", "active",
     "S7.3: load Soft tunnel CSV → FlAeroModel table provider + correlate vs parametric."},
    {FL_PLANNED_HIL, "hardware_in_the_loop", "active",
     "S7.5: Soft SQHIL1 mock PCB framing + correlate (NR Ch.3.1); live serial deferred."},
};

const FlPlannedSurface *fl_planned_list(int *n) {
    if (n) *n = (int)(sizeof k_planned / sizeof k_planned[0]);
    return k_planned;
}

int fl_planned_cfd_mesh_import(const char *path, char *err, int err_cap) {
    (void)path;
    if (err && err_cap)
        snprintf(err, err_cap, "planned: cfd_mesh_import is not implemented in scaffold v0");
    return -1;
}

int fl_planned_wind_tunnel_correlate(const char *path, char *err, int err_cap) {
    /* Legacy stub entry: real path is fl_aero_load_tunnel_csv + correlate via CLI. */
    if (err && err_cap)
        snprintf(err, err_cap,
                 "use: squirrelflightlab tunnel %s  (S7.3 CSV provider + correlate)",
                 path ? path : "FILE.csv");
    return -1;
}

int fl_planned_hil_connect(const char *endpoint, char *err, int err_cap) {
    /* Legacy stub entry: real path is fl_hil_connect via CLI `hil`. */
    if (err && err_cap)
        snprintf(err, err_cap,
                 "use: squirrelflightlab hil %s  (S7.5 Soft mock / fixture; live PCB deferred)",
                 endpoint ? endpoint : "mock");
    return -1;
}

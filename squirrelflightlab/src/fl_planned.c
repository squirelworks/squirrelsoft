#include "fl_planned.h"

#include <stdio.h>

static const FlPlannedSurface k_planned[] = {
    {FL_PLANNED_CFD_MESH, "cfd_mesh_import", "planned",
     "Import volume/surface meshes for higher-fidelity aero correlation. Not wired in v0."},
    {FL_PLANNED_WIND_TUNNEL, "wind_tunnel_correlate", "planned",
     "Compare parametric CL/CD/Cm tables to tunnel runs. Stub only."},
    {FL_PLANNED_HIL, "hardware_in_the_loop", "planned",
     "Talk to the real ISM avionics PCB over serial/USB for HIL. Stub only."},
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
    (void)path;
    if (err && err_cap)
        snprintf(err, err_cap, "planned: wind_tunnel_correlate is not implemented in scaffold v0");
    return -1;
}

int fl_planned_hil_connect(const char *endpoint, char *err, int err_cap) {
    (void)endpoint;
    if (err && err_cap)
        snprintf(err, err_cap, "planned: hardware_in_the_loop is not implemented in scaffold v0");
    return -1;
}

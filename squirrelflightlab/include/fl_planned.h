/*
 * Scaffold / planned surfaces — stubbed, not implemented.
 * CFD mesh import, wind-tunnel correlation, hardware-in-the-loop.
 */
#ifndef FL_PLANNED_H
#define FL_PLANNED_H

typedef enum {
    FL_PLANNED_CFD_MESH = 0,
    FL_PLANNED_WIND_TUNNEL,
    FL_PLANNED_HIL,
    FL_PLANNED_COUNT
} FlPlannedId;

typedef struct {
    FlPlannedId id;
    const char *name;
    const char *status; /* always "planned" in v0 */
    const char *note;
} FlPlannedSurface;

const FlPlannedSurface *fl_planned_list(int *n);

/* Attempting any planned op returns -1 and a message. */
int fl_planned_cfd_mesh_import(const char *path, char *err, int err_cap);
int fl_planned_wind_tunnel_correlate(const char *path, char *err, int err_cap);
int fl_planned_hil_connect(const char *endpoint, char *err, int err_cap);

#endif

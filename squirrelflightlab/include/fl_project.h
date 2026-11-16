/*
 * Project workspace: ISM fighter-jet design inputs.
 * Portable C11 core — no file I/O; the CLI loads text into a buffer.
 */
#ifndef FL_PROJECT_H
#define FL_PROJECT_H

#include "fl_units.h"

typedef struct {
    char name[FL_NAME_LEN];
    int used;
    /* hinge along +Y for elev/rudder-style; ailerons may use +X */
    double chord_mm;
    double span_mm;
    double area_mm2;
    double hinge_x_mm; /* body station of hinge line */
    double max_deflect_deg;
    int channel; /* actuator channel index, or -1 */
} FlSurface;

typedef struct {
    char name[FL_NAME_LEN];
    int used;
    char role[32]; /* imu_ax, radio_throttle, act_elevator, ... */
    int header_pin; /* KiCad header pin number from device doc */
    double scale;   /* engineering units per LSB or per normalised */
    double bias;
} FlChannel;

typedef struct {
    char name[FL_NAME_LEN];
    char notes[160];

    /* Airframe (mm / g) — hardware-doc scale */
    double fuselage_length_mm;
    double fuselage_scale; /* 1.0 = nominal ISM size */
    double wing_span_mm;
    double wing_root_chord_mm;
    double wing_tip_chord_mm;
    double wing_area_mm2;
    double wing_sweep_deg;
    double hstab_area_mm2;
    double vstab_area_mm2;

    /* Mass / CG stubs */
    double mass_g;
    double cg_x_mm; /* from nose */
    double cg_y_mm;
    double cg_z_mm;
    double ixx_g_mm2; /* inertia stubs about CG */
    double iyy_g_mm2;
    double izz_g_mm2;

    /* Propulsion stub */
    double thrust_max_n;
    double throttle_idle;

    /* Study-model / RC / full-scale label (inputs stay mm/g; dyn uses SI). */
    FlScaleMode scale_mode;

    FlSurface surfaces[FL_MAX_SURFACES];
    int nsurfaces;

    FlChannel channels[FL_MAX_CHANNELS];
    int nchannels;

    /* Expected device-doc channel roles (for avionics co-check) */
    char device_doc_id[FL_NAME_LEN];
} FlProject;

void fl_project_default_ism_fighter(FlProject *p);
int fl_project_parse(FlProject *p, const char *text, char *err, int err_cap);
int fl_project_format(const FlProject *p, char *out, int out_cap);
const FlSurface *fl_project_surface(const FlProject *p, const char *name);
const FlChannel *fl_project_channel(const FlProject *p, const char *role);

#endif

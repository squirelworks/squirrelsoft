/*
 * Fighter Jet Flight Lab — units, axes, and shared constants.
 * Geometry from ISM hardware docs is millimetres; dynamics use SI.
 * Original SquirrelSoft code.
 */
#ifndef FL_UNITS_H
#define FL_UNITS_H

#include <math.h>

/* Body axes (aircraft): +X nose, +Y right wing, +Z down.
 * Earth: flat NED for the scaffold (+X north, +Y east, +Z down).
 * Positive rotations: right-hand about each body axis (roll/pitch/yaw).
 * Source of truth for airframe lengths, mass stubs and avionics pin maps:
 * the ISM fighter-jet hardware docs / KiCad bay (external repo). Values
 * shipped here are scaffold placeholders until that doc is linked. */

#define FL_PI 3.14159265358979323846
#define FL_DEG2RAD (FL_PI / 180.0)
#define FL_RAD2DEG (180.0 / FL_PI)
#define FL_G0 9.80665 /* m/s^2 */

/* Project / ISM study inputs are mm and g; dynamics use SI only. */
static inline double fl_mm_to_m(double mm) { return mm * 0.001; }
static inline double fl_m_to_mm(double m) { return m * 1000.0; }
static inline double fl_g_to_kg(double g) { return g * 0.001; }
static inline double fl_kg_to_g(double kg) { return kg * 1000.0; }
/* g·mm² → kg·m²: (1e-3 kg) * (1e-3 m)^2 = 1e-9 */
static inline double fl_g_mm2_to_kg_m2(double g_mm2) { return g_mm2 * 1e-9; }
static inline double fl_deg(double rad) { return rad * FL_RAD2DEG; }
static inline double fl_rad(double deg) { return deg * FL_DEG2RAD; }

typedef enum {
    FL_SCALE_STUDY = 0, /* ~420 mm fuselage placeholders */
    FL_SCALE_RC,        /* Eric RC targets (future) */
    FL_SCALE_FULL       /* ISM full-scale (future) */
} FlScaleMode;

/* Sea-level ISA density stub (kg/m^3). Replaceable aero atmosphere later. */
#define FL_RHO_SL 1.225

#define FL_NAME_LEN 64
#define FL_PATH_LEN 256
#define FL_MAX_SURFACES 8
#define FL_MAX_CHANNELS 32
#define FL_MAX_HISTORY 4096
#define FL_MAX_SWEEP 256
#define FL_MAX_TUNNEL 64
#define FL_MAX_MSG 240

#endif

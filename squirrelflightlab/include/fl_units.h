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

static inline double fl_mm_to_m(double mm) { return mm * 0.001; }
static inline double fl_m_to_mm(double m) { return m * 1000.0; }
static inline double fl_g_to_kg(double g) { return g * 0.001; }
static inline double fl_deg(double rad) { return rad * FL_RAD2DEG; }
static inline double fl_rad(double deg) { return deg * FL_DEG2RAD; }

/* Sea-level ISA density stub (kg/m^3). Replaceable aero atmosphere later. */
#define FL_RHO_SL 1.225

#define FL_NAME_LEN 64
#define FL_PATH_LEN 256
#define FL_MAX_SURFACES 8
#define FL_MAX_CHANNELS 32
#define FL_MAX_HISTORY 4096
#define FL_MAX_SWEEP 256
#define FL_MAX_MSG 240

#endif

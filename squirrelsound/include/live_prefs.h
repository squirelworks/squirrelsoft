#ifndef LIVE_PREFS_H
#define LIVE_PREFS_H

#include "live_engine.h"

#include <stddef.h>

/* Small key=value preferences file for the live view, so settings such as the
 * waterfall direction survive between sessions.
 *
 * Default location:
 *   $XDG_CONFIG_HOME/squirrelsound/live.conf, else ~/.config/squirrelsound/live.conf
 *   (Windows: %APPDATA%\squirrelsound\live.conf)
 * SQSOUND_PREFS=<file> overrides the location (used by tests). */

/* Writes the prefs file path into buf. Returns 0 on success, -1 if no
 * location could be determined or buf is too small. */
int live_prefs_path(char *buf, size_t n);

/* Reads direction= from path. Returns 0 and sets *out if present and valid,
 * -1 otherwise (missing file, missing key, unknown value). */
int live_prefs_load_direction(const char *path, LiveDirection *out);

/* Writes direction= to path, keeping any other lines in the file. Creates the
 * parent directory if needed. Returns 0 on success, -1 on failure. */
int live_prefs_save_direction(const char *path, LiveDirection d);

/* Generic key=value access (keys without '=' or line breaks). get copies
 * the value into val and returns 0, or -1 if missing or too long; set
 * replaces the key and keeps the other lines. */
int live_prefs_get(const char *path, const char *key, char *val, size_t n);
int live_prefs_set(const char *path, const char *key, const char *value);

/* Calibration offset in dB for one input, keyed by its source name
 * (e.g. "mic: USB Audio"), stored as cal.<source>=<dB>. Load returns -1
 * when there is none or it is out of -100..100. */
int live_prefs_load_cal(const char *path, const char *source, double *db);
int live_prefs_save_cal(const char *path, const char *source, double db);

#endif

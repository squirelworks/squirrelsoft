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

#endif

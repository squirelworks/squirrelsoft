#ifndef LIVE_VIEW_H
#define LIVE_VIEW_H

#include "live_audio.h"
#include "live_engine.h"

/* Run interactive SDL spectrum + waterfall. Returns 0 on clean exit.
 * Requires SQSOUND_HAS_SDL at build time.
 * direction is the starting waterfall direction; the w key toggles it live,
 * and each toggle is saved to prefs_path when that is non-NULL. */
int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine, LiveDirection direction,
                      const char *prefs_path);

/* Headless / CI: process frames_max frames, print peak stats. */
int live_view_headless_run(LiveAudio *audio, LiveEngine *engine, int frames_max,
                           int print_every);

/* Headless: process frames_max frames, then draw the Live window (the same
 * sqgui layout as the SDL view) at w x h into a binary PPM. */
int live_view_gui_snapshot(LiveAudio *audio, LiveEngine *engine, LiveDirection direction,
                           int frames_max, int w, int h, const char *ppm_path);

#endif

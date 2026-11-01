#ifndef LIVE_VIEW_H
#define LIVE_VIEW_H

#include "live_audio.h"
#include "live_engine.h"

/* Run interactive SDL spectrum + waterfall. Returns 0 on clean exit.
 * Requires SQSOUND_HAS_SDL at build time. */
int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine);

/* Headless / CI: process frames_max frames, print peak stats. */
int live_view_headless_run(LiveAudio *audio, LiveEngine *engine, int frames_max,
                           int print_every);

#endif

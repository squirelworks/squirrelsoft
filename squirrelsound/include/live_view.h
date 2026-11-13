#ifndef LIVE_VIEW_H
#define LIVE_VIEW_H

#include "live_audio.h"
#include "live_engine.h"

/* How to run the live view */
typedef struct {
    LiveDirection direction;  /* starting waterfall direction */
    const char *prefs_path;   /* direction and calibration are saved here (NULL: not saved) */
    const char *source_label; /* "mic: USB Audio"; status line and the calibration key */
    const char *record_path;  /* record from the start into this WAV (NULL: not) */
    int view;                 /* LIVE_VIEW_* for the window and the snapshot */
    int frames;               /* headless and snapshot: frames to process */
    int print_every;          /* headless: print every n frames */
    int width, height;        /* snapshot size */
    const char *snapshot;     /* snapshot: PPM path */
} LiveViewOptions;

/* Interactive SDL window (live_gui on libs/sqgui). Returns 0 on a clean
 * exit. Requires SQSOUND_HAS_SDL at build time. */
int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine, const LiveViewOptions *o);

/* Headless / CI: process o->frames frames, print peak stats. */
int live_view_headless_run(LiveAudio *audio, LiveEngine *engine, const LiveViewOptions *o);

/* Headless: process o->frames frames, then draw the window (the same
 * sqgui layout as the SDL view) at width x height into o->snapshot. */
int live_view_gui_snapshot(LiveAudio *audio, LiveEngine *engine, const LiveViewOptions *o);

/* "demo", "mic: USB Audio": the source as shown and as the calibration key */
void live_view_source_label(LiveAudio *audio, char *buf, size_t n);

#endif

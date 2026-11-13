#ifndef LIVE_GUI_H
#define LIVE_GUI_H

/* The SquirrelSound Live window drawn with libs/sqgui (S3.2 split view).
 * Same layout as the Android Live tab: a two-row toolbar, then the
 * spectrum in the top half and the LOFARgram (waterfall or fountain) in
 * the bottom half. Everything here draws into an sqgui canvas, so the
 * SDL window (live_view_sdl.c), a PPM snapshot and the tests share it. */

#include "live_engine.h"
#include "sqgui.h"

enum { LIVE_RANGE_HUMAN, LIVE_RANGE_MUSIC, LIVE_RANGE_BASS, LIVE_RANGE_SPEECH, LIVE_RANGE_FULL,
       LIVE_RANGE_COUNT };
enum { LIVE_SPEED_SLOW, LIVE_SPEED_NORMAL, LIVE_SPEED_FAST };

typedef struct {
    int paused;
    int range;          /* LIVE_RANGE_*, or -1 after a zoom or pan */
    int speed;          /* LIVE_SPEED_* */
    int fountain;       /* 1: newest row at the bottom */
    int dir_changed;    /* set when the direction was switched; the caller saves it */
    int scroll_phase;   /* frames since the last waterfall push */
    const char *source; /* shown in the status line */
    sqg_rect spectrum, waterfall; /* where the plots went in the last frame */
} LiveGui;

void live_gui_init(LiveGui *g, LiveEngine *e, LiveDirection dir, const char *source);
LiveDirection live_gui_direction(const LiveGui *g);

/* Frames to wait between waterfall pushes: slow 4, normal 2, fast 1 */
int live_gui_scroll_skip(const LiveGui *g);

/* Whether this frame's audio frame should go to the engine (counts the
 * scroll phase); the caller reads audio either way. */
int live_gui_take_frame(LiveGui *g);

void live_gui_set_range(LiveGui *g, LiveEngine *e, int range);

/* Keyboard shortcuts, as on the earlier SDL view: space pause, m max
 * hold, r reset hold, 1-5 range presets, 8/9/0 slow/normal/fast, w
 * waterfall/fountain, [ and ] zoom; key is the ASCII character or one
 * of SQG_KEY_LEFT / SQG_KEY_RIGHT (pan). Returns 1 if q or Esc. */
int live_gui_key(LiveGui *g, LiveEngine *e, int ascii, int sqg_key);

/* Zoom by factor (< 1 in, > 1 out) around frequency fraction t (0..1) */
void live_gui_zoom(LiveGui *g, LiveEngine *e, double t, double factor);

/* Draw one frame and apply what the user did to the widgets */
void live_gui_frame(LiveGui *g, sqg_ui *ui, LiveEngine *e);

#endif

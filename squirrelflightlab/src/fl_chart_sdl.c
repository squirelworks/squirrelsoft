/* Soft S7.4′: SDL strip-chart window around fl_chart (same core as PPM). */
#include "fl_chart_gui.h"
#include "fl_units.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

int fl_chart_gui_run(const FlProject *p, const FlChartStyle *st, double duration_s,
                     double dt, int max_frames) {
    if (!p || !st || st->nch < 1) return -1;
    int w = st->w > 0 ? st->w : 640;
    int h = st->h > 0 ? st->h : 360;
    sqg_window *win = sqg_window_open("SquirrelFlightLab strip-chart", w, h);
    if (!win) {
        fprintf(stderr, "squirrelflightlab: could not open strip window: %s\n",
                SDL_GetError());
        return 2;
    }
    sqg_input in;
    memset(&in, 0, sizeof in);
    int n = 0, segs = 0;
    while ((max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in)) {
        if (in.key == SQG_KEY_ESCAPE) break;
        sqg_canvas *cv = sqg_window_canvas(win);
        segs = fl_chart_render_demo(cv, p, st, duration_s, dt);
        if (segs < 1) {
            sqg_window_close(win);
            return 1;
        }
        sqg_window_present(win);
        n++;
        SDL_Delay(16);
    }
    sqg_window_close(win);
    if (max_frames > 0)
        printf("strip-chart-gui frames=%d segments=%d (NR Ch.3.1 resample)\n", n, segs);
    return 0;
}

int fl_chart_gui_run_hil(const char *endpoint, const char *role0, const char *role1,
                         const FlChartStyle *st, int max_frames, char *err, int err_cap) {
    if (!endpoint || !st || !role0) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    int w = st->w > 0 ? st->w : 640;
    int h = st->h > 0 ? st->h : 360;
    sqg_window *win = sqg_window_open("SquirrelFlightLab hil-chart", w, h);
    if (!win) {
        if (err && err_cap)
            snprintf(err, err_cap, "could not open hil window: %s", SDL_GetError());
        return 2;
    }
    sqg_input in;
    memset(&in, 0, sizeof in);
    int n = 0, segs = 0;
    char local_err[FL_MAX_MSG];
    while ((max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in)) {
        if (in.key == SQG_KEY_ESCAPE) break;
        sqg_canvas *cv = sqg_window_canvas(win);
        segs = fl_chart_render_hil(cv, endpoint, role0, role1, st, local_err,
                                   (int)sizeof local_err);
        if (segs < 1) {
            if (err && err_cap)
                snprintf(err, err_cap, "%s", local_err[0] ? local_err : "draw failed");
            sqg_window_close(win);
            return 1;
        }
        sqg_window_present(win);
        n++;
        SDL_Delay(16);
    }
    sqg_window_close(win);
    if (max_frames > 0)
        printf("hil-chart-gui frames=%d segments=%d (NR Ch.3.1 resample)\n", n, segs);
    return 0;
}

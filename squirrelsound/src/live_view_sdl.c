/* SquirrelSound Live window: libs/sqgui SDL back end around live_gui
 * (S3.2 split view). Original code. */
#include "live_view.h"
#include "live_gui.h"
#include "live_prefs.h"
#include "live_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SQSOUND_HAS_SDL
#include <SDL2/SDL.h>

static void save_cal(const LiveViewOptions *o, const char *label, double db) {
    if (o->prefs_path && live_prefs_save_cal(o->prefs_path, label, db) != 0)
        fprintf(stderr, "warning: could not save %s\n", o->prefs_path);
}

int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine, const LiveViewOptions *o) {
    char title[400], label[300];
    live_view_source_label(audio, label, sizeof label);
    const char *source = o->source_label ? o->source_label : label;
    snprintf(title, sizeof title, "SquirrelSound Live \u2014 %s", source);

    sqg_window *win = sqg_window_open(title, 960, 640);
    if (!win) {
        fprintf(stderr, "SquirrelSound Live: could not open a window: %s\n", SDL_GetError());
        return -1;
    }
    size_t nfft = engine->cfg.fft_size;
    double *buf = (double *)malloc(nfft * sizeof(double));
    if (!buf) {
        sqg_window_close(win);
        return -1;
    }

    LiveGui g;
    live_gui_init(&g, engine, o->direction, source);
    if (o->view > 0 && o->view < LIVE_VIEW_COUNT) g.view = o->view;
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = { 0 };
    LiveRecorder rec;
    memset(&rec, 0, sizeof rec);
    char rec_info[300] = "";
    const char *rec_path = o->record_path;
    g.rec_info = rec_info;

    printf("SquirrelSound Live\n");
    printf("  q/Esc quit | space pause | m max-hold | r reset hold | p peak labels\n");
    printf("  [/] zoom | arrows pan | wheel zoom | v next view | R record | -/= calibration\n");
    printf("  1 Human 20-16k | 2 Music 20-4k | 3 Bass 20-250 | 4 Speech | 5 Full\n");
    printf("  8/9/0 scroll Slow/Normal/Fast | w waterfall/fountain\n");
    printf("  direction=%s  source=%s  fft=%zu  fs=%.0f  cal=%+.1f dB\n",
           live_direction_name(o->direction), source, nfft, engine->cfg.sample_rate,
           engine->cfg.cal_db);

    live_audio_start(audio);
    if (o->record_path) {
        g.record = 1;
        g.record_changed = 1;
    }
    int running = 1;
    while (running && sqg_window_poll(win, &in)) {
        /* Keys first, so a shortcut and a click in one frame both apply */
        if (live_gui_key(&g, engine, 0, in.key)) running = 0;
        for (const char *t = in.text; *t && running; t++)
            if (live_gui_key(&g, engine, (unsigned char)*t, SQG_KEY_NONE)) running = 0;

        /* Audio keeps flowing while stopped so the source never backs up */
        for (int i = 0; i < 4; i++) live_audio_poll(audio);
        live_record_pump(&rec, audio);
        if (rec.full && g.record) { /* 32-bit WAV limit reached: close this take */
            fprintf(stderr, "\n%s is full (WAV size limit); recording stopped\n", rec.path);
            g.record = 0;
            g.record_changed = 1;
        }
        if (live_audio_read(audio, buf, nfft) >= nfft && !g.paused && live_gui_take_frame(&g))
            live_engine_process(engine, buf);

        live_record_info(&rec, rec_info, sizeof rec_info);
        sqg_canvas *cv = sqg_window_canvas(win);
        sqg_begin(&ui, cv, &in);
        live_gui_frame(&g, &ui, engine);
        sqg_end(&ui);
        sqg_window_present(win);

        if (g.record_changed) {
            g.record_changed = 0;
            if (g.record && !live_record_active(&rec)) {
                if (live_record_start(&rec, audio, rec_path) != 0) {
                    fprintf(stderr, "cannot record to %s\n", rec.path);
                    g.record = 0;
                } else {
                    printf("\nrecording to %s\n", rec.path);
                }
                rec_path = NULL; /* a second Rec makes a new, dated file */
            } else if (!g.record && live_record_active(&rec)) {
                double secs = live_record_seconds(&rec);
                char path[sizeof rec.path];
                snprintf(path, sizeof path, "%s", rec.path);
                if (live_record_stop(&rec, audio) != 0)
                    fprintf(stderr, "error writing %s\n", path);
                else
                    printf("recorded %.2f s to %s\n", secs, path);
            }
        }
        if (g.dir_changed) {
            g.dir_changed = 0;
            if (o->prefs_path &&
                live_prefs_save_direction(o->prefs_path, live_gui_direction(&g)) != 0)
                fprintf(stderr, "warning: could not save %s\n", o->prefs_path);
        }
        if (g.cal_changed) {
            g.cal_changed = 0;
            save_cal(o, source, engine->cfg.cal_db);
        }
        SDL_Delay(16);
    }
    if (live_record_active(&rec)) {
        char path[sizeof rec.path];
        snprintf(path, sizeof path, "%s", rec.path);
        double secs = live_record_seconds(&rec);
        if (live_record_stop(&rec, audio) == 0) printf("recorded %.2f s to %s\n", secs, path);
    }
    live_audio_stop(audio);
    free(buf);
    sqg_window_close(win);
    return 0;
}

#else /* !SQSOUND_HAS_SDL */

int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine, const LiveViewOptions *o) {
    (void)audio;
    (void)engine;
    (void)o;
    fprintf(stderr, "SDL UI not built into this binary. Rebuild with SDL2 or use "
                    "--headless / --gui-snapshot.\n");
    return -1;
}

#endif

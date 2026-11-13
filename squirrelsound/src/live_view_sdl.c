/* SquirrelSound Live window: libs/sqgui SDL back end around live_gui
 * (S3.2 split view). Original code. */
#include "live_view.h"
#include "live_gui.h"
#include "live_prefs.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef SQSOUND_HAS_SDL
#include <SDL2/SDL.h>

int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine, LiveDirection direction,
                      const char *prefs_path) {
    char title[400], source[200];
    const char *dev = live_audio_device_name(audio);
    snprintf(source, sizeof source, "%s%s%s", live_audio_kind_name(live_audio_kind(audio)),
             *dev ? ": " : "", dev);
    snprintf(title, sizeof title, "SquirrelSound Live — %s", source);

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
    live_gui_init(&g, engine, direction, source);
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = { 0 };

    printf("SquirrelSound Live\n");
    printf("  q/Esc quit | space pause | m max-hold | r reset hold\n");
    printf("  [/] zoom | arrows pan | wheel zoom\n");
    printf("  1 Human 20-16k | 2 Music 20-4k | 3 Bass 20-250 | 4 Speech | 5 Full\n");
    printf("  8/9/0 scroll Slow/Normal/Fast | w waterfall/fountain\n");
    printf("  direction=%s  source=%s  fft=%zu  fs=%.0f\n", live_direction_name(direction),
           source, nfft, engine->cfg.sample_rate);

    live_audio_start(audio);
    int running = 1;
    while (running && sqg_window_poll(win, &in)) {
        /* Keys first, so a shortcut and a click in one frame both apply */
        if (live_gui_key(&g, engine, 0, in.key)) running = 0;
        for (const char *t = in.text; *t && running; t++)
            if (live_gui_key(&g, engine, (unsigned char)*t, SQG_KEY_NONE)) running = 0;

        /* Audio keeps flowing while stopped so the source never backs up */
        for (int i = 0; i < 4; i++) live_audio_poll(audio);
        if (live_audio_read(audio, buf, nfft) >= nfft && !g.paused && live_gui_take_frame(&g))
            live_engine_process(engine, buf);

        sqg_canvas *cv = sqg_window_canvas(win);
        sqg_begin(&ui, cv, &in);
        live_gui_frame(&g, &ui, engine);
        sqg_end(&ui);
        sqg_window_present(win);

        if (g.dir_changed) {
            g.dir_changed = 0;
            if (prefs_path &&
                live_prefs_save_direction(prefs_path, live_gui_direction(&g)) != 0)
                fprintf(stderr, "warning: could not save %s\n", prefs_path);
        }
        SDL_Delay(16);
    }
    live_audio_stop(audio);
    free(buf);
    sqg_window_close(win);
    return 0;
}

#else /* !SQSOUND_HAS_SDL */

int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine, LiveDirection direction,
                      const char *prefs_path) {
    (void)audio;
    (void)engine;
    (void)direction;
    (void)prefs_path;
    fprintf(stderr, "SDL UI not built into this binary. Rebuild with SDL2 or use "
                    "--headless / --gui-snapshot.\n");
    return -1;
}

#endif

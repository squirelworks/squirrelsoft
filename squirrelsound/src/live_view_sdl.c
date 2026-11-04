#include "live_view.h"
#include "live_prefs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SQSOUND_HAS_SDL
#include <SDL2/SDL.h>

static Uint32 db_to_rgb(float db, float dmin, float dmax) {
    float span = dmax - dmin;
    if (span < 1.0f) span = 1.0f;
    return (Uint32)live_color_argb((db - dmin) / span);
}

int live_view_sdl_run(LiveAudio *audio, LiveEngine *engine, LiveDirection direction,
                      const char *prefs_path) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    int W = 960, H = 640;
    SDL_Window *win = SDL_CreateWindow(
        "SquirrelSound Live — spectrum + waterfall", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    {
        char title[400];
        const char *dev = live_audio_device_name(audio);
        snprintf(title, sizeof title, "SquirrelSound Live \u2014 %s%s%s", live_audio_kind_name(live_audio_kind(audio)),
                 *dev ? ": " : "", dev);
        SDL_SetWindowTitle(win, title);
    }
    SDL_Renderer *ren =
        SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return -1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    size_t nfft = engine->cfg.fft_size;
    double *buf = (double *)malloc(nfft * sizeof(double));
    Uint32 *pix = (Uint32 *)malloc((size_t)W * (size_t)H * sizeof(Uint32));
    SDL_Texture *tex =
        SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!buf || !pix || !tex) {
        free(buf);
        free(pix);
        if (tex) SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return -1;
    }

    live_audio_start(audio);
    int running = 1;
    int paused = 0;
    double zoom_fmin = engine->cfg.f_min_hz;
    double zoom_fmax = engine->cfg.f_max_hz;
    Uint32 last_print = 0;

    /* Match Android Basic presets: Human / Music / Bass / Speech / Full. */
    double nyq = engine->cfg.sample_rate * 0.5;
    live_engine_set_freq_range(engine, 20.0, nyq < 16000.0 ? nyq : 16000.0);
    zoom_fmin = engine->cfg.f_min_hz;
    zoom_fmax = engine->cfg.f_max_hz;
    int scroll_skip = 1; /* 1=fast, 2=normal, 4=slow (frames between waterfall pushes) */
    int scroll_phase = 0;

    printf("SquirrelSound Live UI\n");
    printf("  q/Esc quit | space pause | m max-hold | r reset hold\n");
    printf("  [/] zoom | arrows pan | wheel zoom\n");
    printf("  1 Human 20-16k | 2 Music 20-4k | 3 Bass 20-250 | 4 Speech | 5 Full\n");
    printf("  8/9/0 scroll Slow/Normal/Fast | w waterfall/fountain\n");
    printf("  direction=%s\n", live_direction_name(direction));
    printf("  source=%s  fft=%zu  fs=%.0f\n", live_audio_kind_name(live_audio_kind(audio)),
           nfft, engine->cfg.sample_rate);

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_q) running = 0;
                if (k == SDLK_SPACE) paused = !paused;
                if (k == SDLK_m) {
                    engine->cfg.max_hold = !engine->cfg.max_hold;
                    if (!engine->cfg.max_hold) live_engine_reset_hold(engine);
                }
                if (k == SDLK_r) live_engine_reset_hold(engine);
                if (k == SDLK_1) { /* Human */
                    live_engine_set_freq_range(engine, 20.0, nyq < 16000.0 ? nyq : 16000.0);
                    zoom_fmin = engine->cfg.f_min_hz;
                    zoom_fmax = engine->cfg.f_max_hz;
                }
                if (k == SDLK_2) { /* Music (bass-friendly) */
                    live_engine_set_freq_range(engine, 20.0, 4000.0);
                    zoom_fmin = engine->cfg.f_min_hz;
                    zoom_fmax = engine->cfg.f_max_hz;
                }
                if (k == SDLK_3) { /* Bass */
                    live_engine_set_freq_range(engine, 20.0, 250.0);
                    zoom_fmin = engine->cfg.f_min_hz;
                    zoom_fmax = engine->cfg.f_max_hz;
                }
                if (k == SDLK_4) { /* Speech */
                    live_engine_set_freq_range(engine, 100.0, 8000.0);
                    zoom_fmin = engine->cfg.f_min_hz;
                    zoom_fmax = engine->cfg.f_max_hz;
                }
                if (k == SDLK_5) { /* Full Nyquist */
                    live_engine_set_freq_range(engine, 20.0, nyq);
                    zoom_fmin = engine->cfg.f_min_hz;
                    zoom_fmax = engine->cfg.f_max_hz;
                }
                if (k == SDLK_8) scroll_skip = 4; /* Slow */
                if (k == SDLK_9) scroll_skip = 2; /* Normal */
                if (k == SDLK_0) scroll_skip = 1; /* Fast */
                if (k == SDLK_w) {
                    direction = (direction == LIVE_DIR_WATERFALL) ? LIVE_DIR_FOUNTAIN
                                                                  : LIVE_DIR_WATERFALL;
                    if (prefs_path && live_prefs_save_direction(prefs_path, direction) != 0)
                        fprintf(stderr, "\nwarning: could not save %s\n", prefs_path);
                }
                if (k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET) {
                    double mid = 0.5 * (zoom_fmin + zoom_fmax);
                    double half = 0.5 * (zoom_fmax - zoom_fmin);
                    half *= (k == SDLK_LEFTBRACKET) ? 0.8 : 1.25;
                    if (half < 20) half = 20;
                    live_engine_set_freq_range(engine, mid - half, mid + half);
                    zoom_fmin = engine->cfg.f_min_hz;
                    zoom_fmax = engine->cfg.f_max_hz;
                }
                if (k == SDLK_LEFT || k == SDLK_RIGHT) {
                    double span = zoom_fmax - zoom_fmin;
                    double step = span * 0.1 * (k == SDLK_LEFT ? -1 : 1);
                    live_engine_set_freq_range(engine, zoom_fmin + step, zoom_fmax + step);
                    zoom_fmin = engine->cfg.f_min_hz;
                    zoom_fmax = engine->cfg.f_max_hz;
                }
            }
            if (ev.type == SDL_MOUSEWHEEL) {
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                double t = (double)mx / (double)W;
                double anchor = zoom_fmin + t * (zoom_fmax - zoom_fmin);
                double half = 0.5 * (zoom_fmax - zoom_fmin);
                half *= (ev.wheel.y > 0) ? 0.85 : 1.15;
                if (half < 20) half = 20;
                live_engine_set_freq_range(engine, anchor - 2 * half * t,
                                           anchor + 2 * half * (1 - t));
                zoom_fmin = engine->cfg.f_min_hz;
                zoom_fmax = engine->cfg.f_max_hz;
            }
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                W = ev.window.data1;
                H = ev.window.data2;
                if (W < 320) W = 320;
                if (H < 240) H = 240;
                free(pix);
                pix = (Uint32 *)malloc((size_t)W * (size_t)H * sizeof(Uint32));
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, W, H);
                if (!pix || !tex) {
                    running = 0;
                    break;
                }
            }
        }

        if (!paused) {
            for (int i = 0; i < 4; i++) live_audio_poll(audio);
            if (live_audio_read(audio, buf, nfft) >= nfft) {
                scroll_phase++;
                if (scroll_phase >= scroll_skip) {
                    scroll_phase = 0;
                    live_engine_process(engine, buf);
                }
            }
        }

        size_t b0 = live_engine_hz_to_bin(engine, engine->cfg.f_min_hz);
        size_t b1 = live_engine_hz_to_bin(engine, engine->cfg.f_max_hz);
        if (b1 <= b0) b1 = b0 + 1;
        double dmin = engine->cfg.db_min, dmax = engine->cfg.db_max;
        int spectrum_h = H / 3;
        int water_h = H - spectrum_h;

        memset(pix, 0x10, (size_t)W * (size_t)H * sizeof(Uint32));
        for (int i = 0; i < W * H; i++) pix[i] = 0xFF0A1018u;

        /* Spectrum polyline into pixels */
        int prev_y = -1;
        for (int x = 0; x < W; x++) {
            size_t k = b0 + (size_t)((double)x / (double)W * (double)(b1 - b0));
            if (k >= engine->bins) k = engine->bins - 1;
            double db = engine->mag[k];
            int y = spectrum_h - 1 -
                    (int)((db - dmin) / (dmax - dmin) * (spectrum_h - 2));
            if (y < 0) y = 0;
            if (y >= spectrum_h) y = spectrum_h - 1;
            if (prev_y >= 0) {
                int y0 = prev_y, y1 = y;
                if (y0 > y1) {
                    int tmp = y0;
                    y0 = y1;
                    y1 = tmp;
                }
                for (int yy = y0; yy <= y1; yy++) pix[yy * W + x] = 0xFF50DCFFu;
            } else {
                pix[y * W + x] = 0xFF50DCFFu;
            }
            prev_y = y;
            if (engine->cfg.max_hold) {
                double hdb = engine->mag_hold[k];
                int hy = spectrum_h - 1 -
                         (int)((hdb - dmin) / (dmax - dmin) * (spectrum_h - 2));
                if (hy < 0) hy = 0;
                if (hy >= spectrum_h) hy = spectrum_h - 1;
                pix[hy * W + x] = 0xFFFFB43Cu;
            }
        }

        if (engine->peak_hz >= engine->cfg.f_min_hz &&
            engine->peak_hz <= engine->cfg.f_max_hz) {
            double t = (engine->peak_hz - engine->cfg.f_min_hz) /
                       (engine->cfg.f_max_hz - engine->cfg.f_min_hz);
            int px = (int)(t * W);
            if (px < 0) px = 0;
            if (px >= W) px = W - 1;
            for (int y = 0; y < spectrum_h; y++) pix[y * W + px] = 0xFFFF5078u;
        }

        for (int y = 0; y < water_h; y++) {
            size_t row = live_waterfall_row_for_y(y, water_h, engine->wf_rows, direction);
            for (int x = 0; x < W; x++) {
                size_t k = b0 + (size_t)((double)x / (double)W * (double)(b1 - b0));
                if (k >= engine->bins) k = engine->bins - 1;
                float db = engine->waterfall[row * engine->bins + k];
                pix[(spectrum_h + y) * W + x] = db_to_rgb(db, (float)dmin, (float)dmax);
            }
        }

        SDL_UpdateTexture(tex, NULL, pix, W * (int)sizeof(Uint32));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        Uint32 now = SDL_GetTicks();
        if (now - last_print > 500) {
            printf("\r peak %8.2f Hz  %6.1f dB  hold=%s  %.0f-%.0f Hz   ",
                   engine->peak_hz, engine->peak_db, engine->cfg.max_hold ? "on" : "off",
                   engine->cfg.f_min_hz, engine->cfg.f_max_hz);
            fflush(stdout);
            last_print = now;
        }
        SDL_Delay(16);
    }

    printf("\n");
    live_audio_stop(audio);
    free(buf);
    free(pix);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
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
                    "--headless / phone web UI.\n");
    return -1;
}

#endif

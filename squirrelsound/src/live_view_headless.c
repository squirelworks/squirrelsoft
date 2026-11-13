#include "live_view.h"
#include "live_gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

int live_view_headless_run(LiveAudio *audio, LiveEngine *engine, int frames_max,
                           int print_every) {
    if (!audio || !engine || frames_max < 1) return -1;
    if (print_every < 1) print_every = 1;
    size_t nfft = engine->cfg.fft_size;
    double *buf = (double *)malloc(nfft * sizeof(double));
    if (!buf) return -1;
    live_audio_start(audio);
    int frames = 0;
    while (frames < frames_max) {
        live_audio_poll(audio);
        /* keep polling until we have a full FFT frame */
        size_t got = live_audio_read(audio, buf, nfft);
        if (got < nfft) {
            sleep_ms(5);
            continue;
        }
        /* drain to keep latency low: re-read newest */
        for (int spin = 0; spin < 8; spin++) {
            live_audio_poll(audio);
            size_t g2 = live_audio_read(audio, buf, nfft);
            if (g2 < nfft) break;
        }
        if (live_engine_process(engine, buf) != 0) break;
        frames++;
        if (frames % print_every == 0) {
            printf("frame %d  peak=%.2f Hz  %.1f dBFS  src=%s\n", frames, engine->peak_hz,
                   engine->peak_db, live_audio_kind_name(live_audio_kind(audio)));
            fflush(stdout);
        }
        sleep_ms(15);
    }
    live_audio_stop(audio);
    free(buf);
    return frames > 0 ? 0 : -1;
}

int live_view_gui_snapshot(LiveAudio *audio, LiveEngine *engine, LiveDirection direction,
                           int frames_max, int w, int h, const char *ppm_path) {
    if (!audio || !engine || !ppm_path || w < 64 || h < 64) return -1;
    size_t nfft = engine->cfg.fft_size;
    double *buf = (double *)malloc(nfft * sizeof(double));
    uint32_t *px = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof *px);
    if (!buf || !px) {
        free(buf);
        free(px);
        return -1;
    }
    char source[200];
    const char *dev = live_audio_device_name(audio);
    snprintf(source, sizeof source, "%s%s%s", live_audio_kind_name(live_audio_kind(audio)),
             *dev ? ": " : "", dev);
    LiveGui g;
    live_gui_init(&g, engine, direction, source);
    live_audio_start(audio);
    int frames = 0, idle = 0;
    while (frames < frames_max && idle < 400) {
        live_audio_poll(audio);
        if (live_audio_read(audio, buf, nfft) < nfft) {
            idle++;
            sleep_ms(5);
            continue;
        }
        idle = 0;
        if (live_engine_process(engine, buf) != 0) break;
        frames++;
    }
    live_audio_stop(audio);

    sqg_canvas cv;
    sqg_canvas_init(&cv, px, w, h, w);
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in;
    memset(&in, 0, sizeof in);
    in.mx = in.my = -1;
    sqg_begin(&ui, &cv, &in);
    live_gui_frame(&g, &ui, engine);
    sqg_end(&ui);
    int rc = sqg_write_ppm(&cv, ppm_path);
    if (rc == 0)
        printf("gui snapshot %dx%d after %d frames  peak=%.2f Hz -> %s\n", w, h, frames,
               engine->peak_hz, ppm_path);
    free(buf);
    free(px);
    return rc == 0 && frames > 0 ? 0 : -1;
}

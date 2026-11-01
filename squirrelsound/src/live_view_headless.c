#include "live_view.h"

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

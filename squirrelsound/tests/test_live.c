#include "live_audio.h"
#include "live_engine.h"

#include <math.h>
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

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

int main(void) {
    LiveAudioConfig acfg;
    memset(&acfg, 0, sizeof(acfg));
    acfg.kind = LIVE_SRC_DEMO;
    acfg.sample_rate = 44100;
    acfg.demo_freq_hz = 1000;
    acfg.demo_harmonics = 1;
    LiveAudio *a = live_audio_open(&acfg);
    expect(a != NULL, "open demo");
    live_audio_start(a);

    LiveEngineConfig ecfg;
    memset(&ecfg, 0, sizeof(ecfg));
    ecfg.fft_size = 2048;
    ecfg.window = SQDSP_WIN_HANN;
    ecfg.sample_rate = 44100;
    ecfg.db_min = -90;
    ecfg.db_max = 0;
    ecfg.f_max_hz = 22050;
    LiveEngine e;
    expect(live_engine_init(&e, &ecfg) == 0, "engine init");

    double *buf = (double *)malloc(2048 * sizeof(double));
    int ok_frames = 0;
    for (int i = 0; i < 80; i++) {
        live_audio_poll(a);
        if (live_audio_read(a, buf, 2048) < 2048) {
            sleep_ms(5);
            continue;
        }
        live_engine_process(&e, buf);
        ok_frames++;
        if (ok_frames >= 10) break;
    }
    expect(ok_frames >= 5, "processed frames");
    expect(fabs(e.peak_hz - 1000.0) < 20.0, "live peak near 1000 Hz");

    free(buf);
    live_engine_free(&e);
    live_audio_close(a);

    if (failures) {
        fprintf(stderr, "%d live failure(s)\n", failures);
        return 1;
    }
    puts("squirrelsound live tests ok");
    return 0;
}

#include "live_audio.h"
#include "live_engine.h"
#include "live_prefs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
#include <unistd.h>
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

static int failures;
static void expect(int c, const char *m);

static void test_direction(void) {
    /* Waterfall: newest (row 0) at the top, oldest at the bottom. */
    expect(live_waterfall_row_for_y(0, 100, 240, LIVE_DIR_WATERFALL) == 0, "waterfall top is newest");
    expect(live_waterfall_row_for_y(99, 100, 240, LIVE_DIR_WATERFALL) == 237, "waterfall bottom is oldest");
    /* Fountain: mirror image. */
    expect(live_waterfall_row_for_y(99, 100, 240, LIVE_DIR_FOUNTAIN) == 0, "fountain bottom is newest");
    expect(live_waterfall_row_for_y(0, 100, 240, LIVE_DIR_FOUNTAIN) == 237, "fountain top is oldest");
    for (int y = 0; y < 100; y++) {
        size_t a = live_waterfall_row_for_y(y, 100, 240, LIVE_DIR_WATERFALL);
        size_t b = live_waterfall_row_for_y(99 - y, 100, 240, LIVE_DIR_FOUNTAIN);
        if (a != b) { expect(0, "fountain mirrors waterfall"); break; }
    }
    expect(live_waterfall_row_for_y(500, 100, 240, LIVE_DIR_WATERFALL) < 240, "y clamped");
    expect(live_waterfall_row_for_y(0, 0, 240, LIVE_DIR_FOUNTAIN) == 0, "zero height safe");

    expect(live_direction_parse("fountain", LIVE_DIR_WATERFALL) == LIVE_DIR_FOUNTAIN, "parse fountain");
    expect(live_direction_parse("waterfall", LIVE_DIR_FOUNTAIN) == LIVE_DIR_WATERFALL, "parse waterfall");
    expect(live_direction_parse("sideways", LIVE_DIR_FOUNTAIN) == LIVE_DIR_FOUNTAIN, "unknown keeps fallback");
    expect(live_direction_parse(NULL, LIVE_DIR_FOUNTAIN) == LIVE_DIR_FOUNTAIN, "NULL keeps fallback");
}

static void test_dbfs_and_colour(void) {
    LiveEngineConfig c;
    memset(&c, 0, sizeof(c));
    c.fft_size = 2048;
    c.window = SQDSP_WIN_HANN;
    c.sample_rate = 48000;
    c.f_max_hz = 24000;
    LiveEngine e;
    expect(live_engine_init(&e, &c) == 0, "dbfs engine init");
    expect(e.cfg.db_min == LIVE_DB_DEFAULT_MIN && e.cfg.db_max == LIVE_DB_DEFAULT_MAX,
           "default range -120..-20");
    expect(e.waterfall[0] == (float)LIVE_DB_FLOOR, "empty waterfall at floor");
    double x[2048];
    const double amps[3] = {1.0, 0.1, 0.001};
    const double want[3] = {0.0, -20.0, -60.0};
    for (int a = 0; a < 3; a++) {
        for (int i = 0; i < 2048; i++) x[i] = amps[a] * sin(2.0 * M_PI * 256.0 * i / 2048.0);
        live_engine_process(&e, x);
        if (fabs(e.peak_db - want[a]) > 0.5) {
            fprintf(stderr, "  amp %g -> %.2f dBFS (want %.0f)\n", amps[a], e.peak_db, want[a]);
            expect(0, "sine reads true dBFS");
        }
    }
    memset(x, 0, sizeof x);
    live_engine_process(&e, x);
    expect(e.mag[10] == LIVE_DB_FLOOR, "silence clamps to floor");
    live_engine_free(&e);

    expect(live_color_argb(0.0f) == 0xFF000000u, "colour floor black");
    expect(live_color_argb(1.0f) == 0xFFFFFFFFu, "colour top white");
    expect(live_color_argb(-1.0f) == 0xFF000000u && live_color_argb(9.0f) == 0xFFFFFFFFu,
           "colour clamps");
    expect(live_color_argb(NAN) == 0xFF000000u, "colour NaN is floor");
    unsigned int y = live_color_argb(4.0f / 6.0f);
    expect(((y >> 16) & 0xFF) > 200 && ((y >> 8) & 0xFF) > 200 && (y & 0xFF) < 50,
           "yellow stop");
}

static void test_prefs(void) {
    char dir[] = "/tmp/sqsound_prefs_XXXXXX";
    if (!mkdtemp(dir)) { expect(0, "mkdtemp"); return; }
    char path[256];
    snprintf(path, sizeof path, "%s/nested/live.conf", dir);
    LiveDirection d = LIVE_DIR_WATERFALL;
    expect(live_prefs_load_direction(path, &d) == -1, "missing prefs file");
    expect(live_prefs_save_direction(path, LIVE_DIR_FOUNTAIN) == 0, "save creates dirs");
    expect(live_prefs_load_direction(path, &d) == 0 && d == LIVE_DIR_FOUNTAIN, "load fountain");

    /* Other keys survive a save; the direction line is replaced, not duplicated. */
    FILE *f = fopen(path, "a");
    if (f) { fputs("other=1\n", f); fclose(f); }
    expect(live_prefs_save_direction(path, LIVE_DIR_WATERFALL) == 0, "save again");
    expect(live_prefs_load_direction(path, &d) == 0 && d == LIVE_DIR_WATERFALL, "load waterfall");
    f = fopen(path, "r");
    int other = 0, dirs = 0;
    char line[64];
    while (f && fgets(line, sizeof line, f)) {
        if (strcmp(line, "other=1\n") == 0) other++;
        if (strncmp(line, "direction=", 10) == 0) dirs++;
    }
    if (f) fclose(f);
    expect(other == 1 && dirs == 1, "other keys kept, one direction line");

    f = fopen(path, "w");
    if (f) { fputs("direction=sideways\n", f); fclose(f); }
    d = LIVE_DIR_FOUNTAIN;
    expect(live_prefs_load_direction(path, &d) == -1 && d == LIVE_DIR_FOUNTAIN, "bad value ignored");

    remove(path);
    char sub[256];
    snprintf(sub, sizeof sub, "%s/nested", dir);
    rmdir(sub);
    rmdir(dir);

    char buf[512];
    setenv("SQSOUND_PREFS", "/tmp/override.conf", 1);
    expect(live_prefs_path(buf, sizeof buf) == 0 && strcmp(buf, "/tmp/override.conf") == 0,
           "SQSOUND_PREFS overrides");
    unsetenv("SQSOUND_PREFS");
    setenv("XDG_CONFIG_HOME", "/x", 1);
    expect(live_prefs_path(buf, sizeof buf) == 0 && strcmp(buf, "/x/squirrelsound/live.conf") == 0,
           "XDG path");
    expect(live_prefs_path(buf, 8) == -1, "small buffer rejected");
}

static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

int main(void) {
    test_direction();
    test_dbfs_and_colour();
#ifndef _WIN32
    test_prefs();
#endif
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
    ecfg.db_min = LIVE_DB_DEFAULT_MIN;
    ecfg.db_max = LIVE_DB_DEFAULT_MAX;
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

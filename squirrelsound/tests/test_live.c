/* mkdtemp needs Darwin extensions when -D_POSIX_C_SOURCE=200809L is set. */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "live_audio.h"
#include "live_engine.h"
#include "live_prefs.h"
#include "live_record.h"
#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define close _close
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
    expect(live_engine_wf_row(&e, 0)[0] == (float)LIVE_DB_FLOOR, "empty waterfall at floor");
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

#ifndef _WIN32
/* Prefs path and mkdtemp/setenv are POSIX; skipped under MinGW (see main). */
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

    /* calibration per input, alongside the direction */
    double cal = 0;
    expect(live_prefs_load_cal(path, "mic: USB Audio", &cal) == -1, "no cal yet");
    expect(live_prefs_save_cal(path, "mic: USB Audio", 3.25) == 0, "save cal");
    expect(live_prefs_save_cal(path, "demo", -1.5) == 0, "save another cal");
    expect(live_prefs_load_cal(path, "mic: USB Audio", &cal) == 0 && cal == 3.25, "load cal");
    expect(live_prefs_load_cal(path, "demo", &cal) == 0 && cal == -1.5, "cal per input");
    expect(live_prefs_save_cal(path, "mic: USB Audio", 4) == 0 &&
               live_prefs_load_cal(path, "mic: USB Audio", &cal) == 0 && cal == 4,
           "cal replaced");
    expect(live_prefs_save_cal(path, "a=b\nc", 1) == 0 && live_prefs_load_cal(path, "a=b\nc", &cal) == 0 &&
               cal == 1, "odd names are made safe");
    char v[32];
    expect(live_prefs_get(path, "direction", v, sizeof v) == 0 && strcmp(v, "sideways") == 0,
           "get returns the raw value");
    expect(live_prefs_get(path, "direction", v, 4) == -1, "too long for the buffer");
    expect(live_prefs_set(path, "k=x", "1") == -1 && live_prefs_set(path, "k", "a\nb") == -1,
           "keys with = and values with newlines refused");
    f = fopen(path, "a");
    if (f) { fputs("cal.demo=banana\ncal.big=500\n", f); fclose(f); }
    expect(live_prefs_load_cal(path, "demo", &cal) == -1, "bad cal value ignored");
    expect(live_prefs_load_cal(path, "big", &cal) == -1, "out-of-range cal ignored");

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
#endif

static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

static void test_pick_loopback(void) {
    const char *names[] = {"Built-in Audio Analog Stereo", "Monitor of HDMI Output",
                           "Monitor of Built-in Audio Analog Stereo", "USB Mic"};
    expect(live_pick_loopback(names, 4, "Built-in Audio Analog Stereo", NULL) == 2, "monitor of the default sink");
    expect(live_pick_loopback(names, 4, "Headphones", NULL) == 1, "first monitor without the default's");
    expect(live_pick_loopback(names, 4, NULL, NULL) == 1, "first monitor without a default sink");
    expect(live_pick_loopback(names, 4, NULL, "hdmi") == 1, "--device picks by name, any case");
    expect(live_pick_loopback(names, 4, NULL, "usb") == 3, "--device may name a plain input");
    expect(live_pick_loopback(names, 4, NULL, "nothing") == -1, "--device with no match");
    expect(live_pick_loopback(names, 1, "Built-in Audio Analog Stereo", NULL) == -1, "no monitors at all");
    expect(live_pick_loopback(names, 0, NULL, NULL) == -1, "no devices");
}

/* Bands, peaks and the calibration offset, on synthetic frames */
static void test_bands_peaks_cal(void) {
    LiveEngineConfig c;
    memset(&c, 0, sizeof c);
    c.fft_size = 4096;
    c.window = SQDSP_WIN_HANN;
    c.sample_rate = 48000;
    c.db_min = LIVE_DB_DEFAULT_MIN;
    c.db_max = LIVE_DB_DEFAULT_MAX;
    c.f_min_hz = 20;
    c.f_max_hz = 20000;
    LiveEngine e;
    expect(live_engine_init(&e, &c) == 0, "bands engine init");
    double *x = (double *)malloc(4096 * sizeof(double));
    for (int i = 0; i < 4096; i++)
        x[i] = 0.5 * sin(2 * M_PI * 1000.0 * i / 48000.0) + 0.05 * sin(2 * M_PI * 5000.0 * i / 48000.0);
    live_engine_process(&e, x);
    expect(memcmp(e.time, x, 4096 * sizeof(double)) == 0, "time copy is the unwindowed frame");

    double cen[64], db[64];
    size_t n = live_engine_bands(&e, 3, cen, db, 64);
    expect(n == 30, "30 third-octave bands 25 Hz..20 kHz");
    expect(fabs(cen[16] - 1000) < 1e-6 && fabs(db[16] + 6.02) < 0.3, "1 kHz band at -6 dBFS");
    expect(fabs(cen[23] - 5011.9) < 0.5 && fabs(db[23] + 26.02) < 0.5, "5 kHz band at -26 dBFS");
    n = live_engine_bands(&e, 1, cen, db, 64);
    expect(n == 10 && fabs(db[5] + 6.02) < 0.3, "octave bands");
    expect(live_engine_bands(&e, 3, cen, db, 4) == 4, "cap respected");

    double hz[5], pdb[5];
    n = live_engine_peaks(&e, e.mag, 5, 100, hz, pdb);
    expect(n == 2, "two peaks");
    expect(n >= 2 && fabs(hz[0] - 1000) < 2 && fabs(hz[1] - 5000) < 2 && pdb[0] > pdb[1],
           "peaks loudest first, interpolated");
    n = live_engine_peaks(&e, e.mag, 1, 100, hz, pdb);
    expect(n == 1 && fabs(hz[0] - 1000) < 2, "max_n 1 keeps the loudest");
    live_engine_set_freq_range(&e, 2000, 20000);
    n = live_engine_peaks(&e, e.mag, 5, 100, hz, pdb);
    expect(n == 1 && fabs(hz[0] - 5000) < 2, "peaks only inside the range");
    expect(live_engine_peaks(&e, NULL, 5, 100, hz, pdb) == 0, "NULL spectrum");

    double p0 = e.peak_db;
    e.cfg.cal_db = 10;
    live_engine_process(&e, x);
    expect(fabs(e.peak_db - p0 - 10) < 1e-6, "cal offset adds to levels");
    free(x);
    live_engine_free(&e);
}

/* The recording tap sees every sample once, and the WAV holds them in order */
static void test_record(void) {
    LiveAudioConfig a;
    memset(&a, 0, sizeof a);
    a.kind = LIVE_SRC_DEMO;
    a.sample_rate = 48000;
    a.demo_freq_hz = 1000;
    a.demo_harmonics = 1;
    LiveAudio *la = live_audio_open(&a);
    expect(la != NULL, "record: open demo");
    if (!la) return;
    live_audio_start(la);
    double tmp[8192], newest[1024];
    size_t dropped = 7;
    expect(live_audio_take(la, tmp, 8192, &dropped) == 0 && dropped == 0, "nothing yet");
    live_audio_poll(la);
    live_audio_poll(la);
    size_t got = live_audio_take(la, tmp, 8192, &dropped);
    live_audio_read(la, newest, 1024);
    expect(got == 2048 && dropped == 0, "two polls, 2048 samples");
    expect(memcmp(tmp + 1024, newest, sizeof newest) == 0, "tap order matches the ring");
    expect(live_audio_take(la, tmp, 8192, &dropped) == 0, "each sample once");
    for (int i = 0; i < 70; i++) live_audio_poll(la); /* more than the ring holds */
    got = live_audio_take(la, tmp, 8192, &dropped);
    expect(got == 8192 && dropped == 70 * 1024 - 65536, "overflow skips the oldest and says so");
    live_audio_take_reset(la);
    expect(live_audio_take(la, tmp, 8192, &dropped) == 0, "reset starts at now");

    char path[] = "/tmp/sqsound_rec_XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    LiveRecorder r;
    expect(live_record_start(&r, la, path) == 0 && live_record_active(&r), "record start");
    for (int i = 0; i < 10; i++) {
        live_audio_poll(la);
        live_record_pump(&r, la);
    }
    char info[200];
    live_record_info(&r, info, sizeof info);
    expect(strncmp(info, "REC 0:00 sqsound_rec_", 21) == 0, "status text");
    expect(fabs(live_record_seconds(&r) - 10240.0 / 48000) < 1e-9, "seconds so far");
    live_audio_poll(la);
    double ring_tail[1024];
    live_audio_read(la, ring_tail, 1024);
    expect(live_record_stop(&r, la) == 0 && !live_record_active(&r), "record stop pumps the rest");
    WavBuffer w;
    memset(&w, 0, sizeof w);
    expect(wav_read(path, &w) == 0 && w.n_samples == 11 * 1024 && w.sample_rate == 48000,
           "WAV header patched with the real length");
    if (w.samples && w.n_samples >= 1024) {
        /* the file ends with exactly what the ring ends with (to 16-bit precision) */
        double maxerr = 0;
        for (int i = 0; i < 1024; i++) {
            double d = fabs(w.samples[w.n_samples - 1024 + i] - ring_tail[i]);
            if (d > maxerr) maxerr = d;
        }
        expect(maxerr < 1.0 / 32000, "recording ends with the newest samples, in order");
    }
    wav_free(&w);
    remove(path);
    expect(live_record_start(&r, la, "/nonexistent/dir/x.wav") == -1, "unwritable path refused");

    /* the 32-bit RIFF limit: the header never wraps, and the overflow is reported */
    WavStream *ws = wav_stream_open(path, 48000);
    expect(ws != NULL, "stream open");
    if (ws) {
        const uint64_t max_frames = (0xFFFFFFFFull - 36) / 2;
        wav_stream_skip_for_test(ws, max_frames - 3);
        double five[5] = {0, 0.1, 0.2, 0.3, 0.4};
        expect(wav_stream_write(ws, five, 5) == -1 && wav_stream_full(ws), "write past the limit refused");
        expect(wav_stream_close(ws) == -1, "close reports the loss");
        FILE *f = fopen(path, "rb");
        unsigned char h[44];
        if (f && fread(h, 1, 44, f) == 44) {
            uint32_t riff = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
            uint32_t data = h[40] | h[41] << 8 | h[42] << 16 | (uint32_t)h[43] << 24;
            expect(data == (uint32_t)(max_frames * 2) && riff == data + 36 && riff >= data,
                   "sizes at the limit, no wrap");
        } else {
            expect(0, "read header back");
        }
        if (f) fclose(f);
        remove(path);
    }
    live_audio_close(la);
}

/* Multi-resolution mode: full-scale tones read 0 dBFS in the bass and the
 * treble; the waterfall ring keeps row 0 the newest. */
static void test_multires_ring(void) {
    LiveEngineConfig c;
    memset(&c, 0, sizeof(c));
    c.fft_size = 4096;
    c.window = SQDSP_WIN_HANN;
    c.sample_rate = 48000;
    c.f_max_hz = 24000;
    c.multires = 3;
    LiveEngine e;
    expect(live_engine_init(&e, &c) == 0, "multires engine init");
    expect(e.cfg.multires == 3 && e.mr.levels == 3, "three levels planned");
    static double x[4096];
    const double hz[2] = {187.5, 8062.5}; /* bin centres of the finest and the coarsest level */
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 4096; i++) x[i] = sin(2.0 * M_PI * hz[t] * i / 48000.0);
        expect(live_engine_process(&e, x) == 0, "multires process");
        if (fabs(e.peak_db) > 0.6 || fabs(e.peak_hz - hz[t]) > 12) {
            fprintf(stderr, "  %.1f Hz -> %.2f dBFS at %.1f Hz\n", hz[t], e.peak_db, e.peak_hz);
            expect(0, "multires tone reads 0 dBFS");
        }
    }
    expect(e.frames == 2, "two frames counted");
    {
        /* the octave band around the treble tone reads it at 0 dB too */
        double cen[32], lv[32];
        size_t nb = live_engine_bands(&e, 1, cen, lv, 32);
        double best = -999;
        for (size_t i = 0; i < nb; i++)
            if (fabs(log2(cen[i] / 8000.0)) < 0.5 && lv[i] > best) best = lv[i];
        if (fabs(best) > 1.0) {
            fprintf(stderr, "  8 kHz octave band %.2f dB\n", best);
            expect(0, "multires band level true");
        }
    }
    size_t k_lo = (size_t)(187.5 / 48000.0 * 4096 + 0.5), k_hi = (size_t)(8062.5 / 48000.0 * 4096 + 0.5);
    const float *r0 = live_engine_wf_row(&e, 0), *r1 = live_engine_wf_row(&e, 1);
    expect(r0[k_hi] > r0[k_lo] + 40, "row 0 holds the newest (treble) frame");
    expect(r1[k_lo] > r1[k_hi] + 40, "row 1 holds the one before (bass)");
    expect(live_engine_wf_row(&e, e.wf_rows - 1)[k_lo] == (float)LIVE_DB_FLOOR, "oldest row still empty");
    /* wrap the ring: the newest stays at row 0 */
    for (size_t f = 0; f < e.wf_rows + 3; f++) live_engine_process(&e, x);
    expect(live_engine_wf_row(&e, 0)[k_hi] > -1 && live_engine_wf_row(&e, e.wf_rows - 1)[k_hi] > -1,
           "ring wraps");
    expect(live_engine_set_multires(&e, 0) == 1 && e.cfg.multires == 0, "multires off");
    for (int i = 0; i < 4096; i++) x[i] = sin(2.0 * M_PI * 187.5 * i / 48000.0);
    live_engine_process(&e, x);
    expect(fabs(e.peak_db) < 0.6, "single FFT still 0 dBFS");
    live_engine_set_multires(&e, 9);
    expect(e.cfg.multires >= 2 && e.cfg.multires <= SQDSP_MR_MAX_LEVELS, "levels capped");
    live_engine_free(&e);
}

int main(void) {
    test_direction();
    test_multires_ring();
    test_bands_peaks_cal();
    test_record();
    test_dbfs_and_colour();
    test_pick_loopback();
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

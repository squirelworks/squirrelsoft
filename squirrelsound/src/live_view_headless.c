/* Headless live runs: peak stats for CI, and a snapshot of the window. */
#include "live_gui.h"
#include "live_record.h"
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

void live_view_source_label(LiveAudio *audio, char *buf, size_t n) {
    const char *dev = live_audio_device_name(audio);
    snprintf(buf, n, "%s%s%s", live_audio_kind_name(live_audio_kind(audio)), *dev ? ": " : "",
             dev);
}

static int start_recording(LiveRecorder *rec, LiveAudio *audio, const char *path) {
    if (!path) return 0;
    if (live_record_start(rec, audio, path) != 0) {
        fprintf(stderr, "cannot record to %s\n", path);
        return -1;
    }
    return 0;
}

static void stop_recording(LiveRecorder *rec, LiveAudio *audio) {
    if (!live_record_active(rec)) return;
    double secs = live_record_seconds(rec);
    size_t dropped = rec->dropped;
    char path[sizeof rec->path];
    snprintf(path, sizeof path, "%s", rec->path);
    if (live_record_stop(rec, audio) != 0) fprintf(stderr, "error writing %s\n", path);
    else printf("recorded %.2f s to %s%s\n", secs, path, dropped ? " (with gaps)" : "");
}

/* Pull until one full frame is in, recording on the way; -1 if the source dried up */
static int next_frame(LiveAudio *audio, LiveRecorder *rec, double *buf, size_t nfft) {
    for (int idle = 0; idle < 400; idle++) {
        live_audio_poll(audio);
        live_record_pump(rec, audio);
        if (live_audio_read(audio, buf, nfft) >= nfft) return 0;
        sleep_ms(5);
    }
    return -1;
}

int live_view_headless_run(LiveAudio *audio, LiveEngine *engine, const LiveViewOptions *o) {
    if (!audio || !engine || !o || o->frames < 1) return -1;
    int every = o->print_every < 1 ? 1 : o->print_every;
    size_t nfft = engine->cfg.fft_size;
    double *buf = (double *)malloc(nfft * sizeof(double));
    if (!buf) return -1;
    LiveRecorder rec;
    memset(&rec, 0, sizeof rec);
    live_audio_start(audio);
    if (start_recording(&rec, audio, o->record_path) != 0) {
        live_audio_stop(audio);
        free(buf);
        return -1;
    }
    int frames = 0;
    while (frames < o->frames) {
        if (next_frame(audio, &rec, buf, nfft) != 0) break;
        if (!o->record_path) {
            /* drain to keep latency low: re-read newest */
            for (int spin = 0; spin < 8; spin++) {
                live_audio_poll(audio);
                if (live_audio_read(audio, buf, nfft) < nfft) break;
            }
        }
        if (live_engine_process(engine, buf) != 0) break;
        frames++;
        if (frames % every == 0) {
            printf("frame %d  peak=%.2f Hz  %.1f dBFS  src=%s\n", frames, engine->peak_hz,
                   engine->peak_db, live_audio_kind_name(live_audio_kind(audio)));
            fflush(stdout);
        }
        sleep_ms(15);
    }
    stop_recording(&rec, audio);
    live_audio_stop(audio);
    free(buf);
    return frames > 0 ? 0 : -1;
}

int live_view_gui_snapshot(LiveAudio *audio, LiveEngine *engine, const LiveViewOptions *o) {
    if (!audio || !engine || !o || !o->snapshot || o->width < 64 || o->height < 64) return -1;
    int w = o->width, h = o->height;
    size_t nfft = engine->cfg.fft_size;
    double *buf = (double *)malloc(nfft * sizeof(double));
    uint32_t *px = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof *px);
    if (!buf || !px) {
        free(buf);
        free(px);
        return -1;
    }
    char label[300];
    live_view_source_label(audio, label, sizeof label);
    LiveGui g;
    live_gui_init(&g, engine, o->direction, o->source_label ? o->source_label : label);
    if (o->view > 0 && o->view < LIVE_VIEW_COUNT) g.view = o->view;
    LiveRecorder rec;
    memset(&rec, 0, sizeof rec);
    live_audio_start(audio);
    int frames = 0;
    if (start_recording(&rec, audio, o->record_path) == 0) {
        while (frames < o->frames) {
            if (next_frame(audio, &rec, buf, nfft) != 0) break;
            if (live_engine_process(engine, buf) != 0) break;
            frames++;
        }
    }
    char info[300];
    live_record_info(&rec, info, sizeof info);
    g.record = live_record_active(&rec);
    g.rec_info = info;

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
    live_gui_free(&g);
    stop_recording(&rec, audio);
    live_audio_stop(audio);
    int rc = sqg_write_ppm(&cv, o->snapshot);
    if (rc == 0)
        printf("gui snapshot %dx%d after %d frames  peak=%.2f Hz -> %s\n", w, h, frames,
               engine->peak_hz, o->snapshot);
    free(buf);
    free(px);
    return rc == 0 && frames > 0 ? 0 : -1;
}

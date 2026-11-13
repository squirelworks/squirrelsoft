/* Live recording to WAV (see live_record.h). Original code. */
#include "live_record.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int live_record_start(LiveRecorder *r, LiveAudio *audio, const char *path) {
    if (!r || !audio) return -1;
    memset(r, 0, sizeof *r);
    if (path && *path) {
        snprintf(r->path, sizeof r->path, "%s", path);
    } else {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        if (tm) strftime(r->path, sizeof r->path, "squirrelsound-%Y%m%d-%H%M%S.wav", tm);
        else snprintf(r->path, sizeof r->path, "squirrelsound.wav");
    }
    double rate = live_audio_sample_rate(audio);
    r->ws = wav_stream_open(r->path, (uint32_t)(rate > 0 ? rate + 0.5 : 44100));
    if (!r->ws) return -1;
    live_audio_take_reset(audio);
    return 0;
}

void live_record_pump(LiveRecorder *r, LiveAudio *audio) {
    if (!r || !r->ws) return;
    double buf[4096];
    for (;;) {
        size_t dropped, n = live_audio_take(audio, buf, sizeof buf / sizeof buf[0], &dropped);
        r->dropped += dropped;
        if (n == 0) break;
        if (wav_stream_write(r->ws, buf, n) != 0) {
            r->full = wav_stream_full(r->ws);
            break;
        }
    }
}

int live_record_stop(LiveRecorder *r, LiveAudio *audio) {
    if (!r || !r->ws) return -1;
    live_record_pump(r, audio);
    int rc = wav_stream_close(r->ws);
    r->ws = NULL;
    return rc;
}

int live_record_active(const LiveRecorder *r) { return r && r->ws; }

double live_record_seconds(const LiveRecorder *r) {
    return r && r->ws ? wav_stream_seconds(r->ws) : 0.0;
}

void live_record_info(const LiveRecorder *r, char *buf, size_t n) {
    if (!live_record_active(r)) {
        if (n) buf[0] = 0;
        return;
    }
    int s = (int)live_record_seconds(r);
    const char *name = strrchr(r->path, '/');
    const char *bs = strrchr(r->path, '\\');
    if (bs && (!name || bs > name)) name = bs;
    snprintf(buf, n, "REC %d:%02d %s", s / 60, s % 60, name ? name + 1 : r->path);
}

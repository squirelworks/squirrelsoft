#ifndef LIVE_AUDIO_H
#define LIVE_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    LIVE_SRC_DEMO = 0,
    LIVE_SRC_WAV,
    LIVE_SRC_ALSA,
    LIVE_SRC_MIC,      /* SDL audio capture: ALSA/PulseAudio/PipeWire, WASAPI, CoreAudio */
    LIVE_SRC_LOOPBACK  /* what the computer is playing: a PulseAudio/PipeWire monitor source */
} LiveSourceKind;

typedef struct LiveAudio LiveAudio;

typedef struct {
    LiveSourceKind kind;
    double sample_rate;
    const char *wav_path;   /* LIVE_SRC_WAV */
    const char *alsa_dev;   /* LIVE_SRC_ALSA, e.g. "default" */
    const char *device;     /* LIVE_SRC_MIC / LOOPBACK: part of the device name, NULL = default */
    double demo_freq_hz;
    int demo_harmonics;
} LiveAudioConfig;

LiveAudio *live_audio_open(const LiveAudioConfig *cfg);
void live_audio_close(LiveAudio *la);

/* Push freshly generated/captured samples into the ring (called by backend). */
int live_audio_start(LiveAudio *la);
void live_audio_stop(LiveAudio *la);

/* Copy up to max_n newest samples into out (oldest→newest). Returns count. */
size_t live_audio_read(LiveAudio *la, double *out, size_t max_n);

/* Recording tap: every sample in order, each once. Copies up to max_n
 * samples that arrived since the last take (oldest first) and returns the
 * count; if more than the ring holds piled up, the oldest are skipped and
 * *dropped (when not NULL) says how many. live_audio_take_reset() starts
 * the tap at "now". */
size_t live_audio_take(LiveAudio *la, double *out, size_t max_n, size_t *dropped);
void live_audio_take_reset(LiveAudio *la);

double live_audio_sample_rate(const LiveAudio *la);
LiveSourceKind live_audio_kind(const LiveAudio *la);
const char *live_audio_kind_name(LiveSourceKind k);

/* Non-blocking poll: pull from device/demo into ring. Call from UI loop. */
int live_audio_poll(LiveAudio *la);

/* Name of the capture device the source opened (mic / loopback), or "". */
const char *live_audio_device_name(const LiveAudio *la);

/* Capture devices as listed by SDL (monitor sources included), one per line
 * with "monitor" or "input" in front, written to out. Returns the count or -1. */
int live_audio_list_devices(FILE *out);

/* Pick the loopback device among capture device names: with want set, the
 * first name containing it; otherwise the monitor of default_sink ("Monitor
 * of <sink>", as PulseAudio and PipeWire name it), else the first monitor.
 * Returns the index, or -1. Pure function, unit-tested. */
int live_pick_loopback(const char *const *names, int n, const char *default_sink, const char *want);

#endif

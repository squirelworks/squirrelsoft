#ifndef LIVE_AUDIO_H
#define LIVE_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    LIVE_SRC_DEMO = 0,
    LIVE_SRC_WAV,
    LIVE_SRC_ALSA
} LiveSourceKind;

typedef struct LiveAudio LiveAudio;

typedef struct {
    LiveSourceKind kind;
    double sample_rate;
    const char *wav_path;   /* LIVE_SRC_WAV */
    const char *alsa_dev;   /* LIVE_SRC_ALSA, e.g. "default" */
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

double live_audio_sample_rate(const LiveAudio *la);
LiveSourceKind live_audio_kind(const LiveAudio *la);
const char *live_audio_kind_name(LiveSourceKind k);

/* Non-blocking poll: pull from device/demo into ring. Call from UI loop. */
int live_audio_poll(LiveAudio *la);

#endif

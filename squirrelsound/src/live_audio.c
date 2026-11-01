#include "live_audio.h"

#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SQSOUND_HAS_ALSA
#include <alsa/asoundlib.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LIVE_RING 65536

struct LiveAudio {
    LiveAudioConfig cfg;
    double ring[LIVE_RING];
    size_t wpos;
    size_t available;
    int running;
    /* demo */
    double phase;
    double t;
    /* wav loop */
    WavBuffer wav;
    size_t wav_pos;
#ifdef SQSOUND_HAS_ALSA
    snd_pcm_t *pcm;
    int16_t *alsa_buf;
    size_t alsa_buf_n;
#endif
};

const char *live_audio_kind_name(LiveSourceKind k) {
    switch (k) {
    case LIVE_SRC_DEMO: return "demo";
    case LIVE_SRC_WAV: return "wav";
    case LIVE_SRC_ALSA: return "alsa";
    }
    return "unknown";
}

static void ring_write(LiveAudio *la, const double *x, size_t n) {
    for (size_t i = 0; i < n; i++) {
        la->ring[la->wpos] = x[i];
        la->wpos = (la->wpos + 1) % LIVE_RING;
        if (la->available < LIVE_RING) la->available++;
    }
}

LiveAudio *live_audio_open(const LiveAudioConfig *cfg) {
    LiveAudio *la = (LiveAudio *)calloc(1, sizeof(LiveAudio));
    if (!la) return NULL;
    la->cfg = *cfg;
    if (la->cfg.sample_rate <= 0) la->cfg.sample_rate = 44100;
    if (la->cfg.demo_freq_hz <= 0) la->cfg.demo_freq_hz = 440;
    if (la->cfg.demo_harmonics < 1) la->cfg.demo_harmonics = 1;

    if (cfg->kind == LIVE_SRC_WAV) {
        if (!cfg->wav_path || wav_read(cfg->wav_path, &la->wav) != 0) {
            free(la);
            return NULL;
        }
        la->cfg.sample_rate = (double)la->wav.sample_rate;
    }
#ifdef SQSOUND_HAS_ALSA
    if (cfg->kind == LIVE_SRC_ALSA) {
        const char *dev = cfg->alsa_dev ? cfg->alsa_dev : "default";
        int err = snd_pcm_open(&la->pcm, dev, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
        if (err < 0) {
            fprintf(stderr, "alsa open: %s\n", snd_strerror(err));
            free(la);
            return NULL;
        }
        err = snd_pcm_set_params(la->pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                 1, (unsigned)la->cfg.sample_rate, 1, 500000);
        if (err < 0) {
            fprintf(stderr, "alsa params: %s\n", snd_strerror(err));
            snd_pcm_close(la->pcm);
            free(la);
            return NULL;
        }
        la->alsa_buf_n = 2048;
        la->alsa_buf = (int16_t *)malloc(la->alsa_buf_n * sizeof(int16_t));
        if (!la->alsa_buf) {
            snd_pcm_close(la->pcm);
            free(la);
            return NULL;
        }
    }
#else
    if (cfg->kind == LIVE_SRC_ALSA) {
        fprintf(stderr, "ALSA support not built; use demo or wav\n");
        free(la);
        return NULL;
    }
#endif
    return la;
}

void live_audio_close(LiveAudio *la) {
    if (!la) return;
    live_audio_stop(la);
    wav_free(&la->wav);
#ifdef SQSOUND_HAS_ALSA
    if (la->pcm) snd_pcm_close(la->pcm);
    free(la->alsa_buf);
#endif
    free(la);
}

int live_audio_start(LiveAudio *la) {
    if (!la) return -1;
    la->running = 1;
#ifdef SQSOUND_HAS_ALSA
    if (la->cfg.kind == LIVE_SRC_ALSA && la->pcm) {
        snd_pcm_prepare(la->pcm);
        snd_pcm_start(la->pcm);
    }
#endif
    return 0;
}

void live_audio_stop(LiveAudio *la) {
    if (!la) return;
    la->running = 0;
#ifdef SQSOUND_HAS_ALSA
    if (la->pcm) snd_pcm_drop(la->pcm);
#endif
}

size_t live_audio_read(LiveAudio *la, double *out, size_t max_n) {
    /* Return the newest max_n samples (or fewer if not yet available). */
    if (!la || !out || max_n == 0) return 0;
    size_t n = max_n;
    if (n > la->available) n = la->available;
    size_t start = (la->wpos + LIVE_RING - n) % LIVE_RING;
    for (size_t i = 0; i < n; i++) out[i] = la->ring[(start + i) % LIVE_RING];
    return n;
}

double live_audio_sample_rate(const LiveAudio *la) {
    return la ? la->cfg.sample_rate : 0;
}

LiveSourceKind live_audio_kind(const LiveAudio *la) {
    return la ? la->cfg.kind : LIVE_SRC_DEMO;
}

int live_audio_poll(LiveAudio *la) {
    if (!la || !la->running) return -1;
    double chunk[2048];
    size_t n = 1024;

    if (la->cfg.kind == LIVE_SRC_DEMO) {
        double fs = la->cfg.sample_rate;
        double f = la->cfg.demo_freq_hz;
        /* slow chirp-ish vibrato so waterfall moves */
        double fmod = f * (1.0 + 0.02 * sin(2.0 * M_PI * 0.25 * la->t));
        for (size_t i = 0; i < n; i++) {
            double s = 0.0;
            for (int h = 1; h <= la->cfg.demo_harmonics; h++)
                s += (0.35 / (double)h) *
                     sin(2.0 * M_PI * fmod * (double)h * la->t + la->phase);
            /* light noise floor */
            s += 0.002 * ((double)((i * 1103515245u + 12345u) & 0xffff) / 65535.0 - 0.5);
            chunk[i] = s;
            la->t += 1.0 / fs;
        }
        ring_write(la, chunk, n);
        return (int)n;
    }

    if (la->cfg.kind == LIVE_SRC_WAV) {
        if (la->wav.n_samples == 0) return 0;
        for (size_t i = 0; i < n; i++) {
            chunk[i] = la->wav.samples[la->wav_pos];
            la->wav_pos++;
            if (la->wav_pos >= la->wav.n_samples) la->wav_pos = 0;
        }
        ring_write(la, chunk, n);
        return (int)n;
    }

#ifdef SQSOUND_HAS_ALSA
    if (la->cfg.kind == LIVE_SRC_ALSA && la->pcm) {
        snd_pcm_sframes_t got = snd_pcm_readi(la->pcm, la->alsa_buf, la->alsa_buf_n);
        if (got == -EAGAIN) return 0;
        if (got < 0) {
            snd_pcm_recover(la->pcm, (int)got, 1);
            return 0;
        }
        for (snd_pcm_sframes_t i = 0; i < got; i++)
            chunk[i] = (double)la->alsa_buf[i] / 32768.0;
        ring_write(la, chunk, (size_t)got);
        return (int)got;
    }
#endif
    return 0;
}

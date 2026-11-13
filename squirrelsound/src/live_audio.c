#include "live_audio.h"

#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SQSOUND_HAS_ALSA
#include <alsa/asoundlib.h>
#endif
#ifdef SQSOUND_HAS_SDL
#include <SDL2/SDL.h>
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
    unsigned long long written, taken; /* totals, for live_audio_take */
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
#ifdef SQSOUND_HAS_SDL
    SDL_AudioDeviceID cap;
#endif
    char device[256];
};

const char *live_audio_kind_name(LiveSourceKind k) {
    switch (k) {
    case LIVE_SRC_DEMO: return "demo";
    case LIVE_SRC_WAV: return "wav";
    case LIVE_SRC_ALSA: return "alsa";
    case LIVE_SRC_MIC: return "mic";
    case LIVE_SRC_LOOPBACK: return "loopback";
    }
    return "unknown";
}

static void ring_write(LiveAudio *la, const double *x, size_t n) {
    for (size_t i = 0; i < n; i++) {
        la->ring[la->wpos] = x[i];
        la->wpos = (la->wpos + 1) % LIVE_RING;
        if (la->available < LIVE_RING) la->available++;
        la->written++;
    }
}

static int contains_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && (hay[i] | 32) == (needle[i] | 32)) i++;
        if (i == n) return 1;
    }
    return n == 0;
}

static int is_monitor(const char *name) { return name && strncmp(name, "Monitor of ", 11) == 0; }

int live_pick_loopback(const char *const *names, int n, const char *default_sink, const char *want) {
    if (want && *want) {
        for (int i = 0; i < n; i++)
            if (names[i] && contains_ci(names[i], want)) return i;
        return -1;
    }
    if (default_sink && *default_sink)
        for (int i = 0; i < n; i++)
            if (is_monitor(names[i]) && strcmp(names[i] + 11, default_sink) == 0) return i;
    for (int i = 0; i < n; i++)
        if (is_monitor(names[i])) return i;
    return -1;
}

#ifdef SQSOUND_HAS_SDL
static int sdl_audio_up(void) {
    /* Monitor sources are hidden from capture lists unless asked for. */
    SDL_SetHint("SDL_AUDIO_INCLUDE_MONITORS", "1");
    if (SDL_WasInit(SDL_INIT_AUDIO)) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

int live_audio_list_devices(FILE *out) {
    if (sdl_audio_up()) return -1;
    int n = SDL_GetNumAudioDevices(1);
    for (int i = 0; i < n; i++) {
        const char *name = SDL_GetAudioDeviceName(i, 1);
        if (name) fprintf(out, "%-8s %s\n", is_monitor(name) ? "monitor" : "input", name);
    }
    fprintf(out, "(audio driver: %s)\n", SDL_GetCurrentAudioDriver());
    return n < 0 ? 0 : n;
}

/* Open the SDL capture device for mic or loopback; mono float at the rate asked. */
static int sdl_capture_open(LiveAudio *la) {
    if (sdl_audio_up()) return -1;
    const char *name = NULL; /* NULL = the system's default input */
    int n = SDL_GetNumAudioDevices(1);
    const char *drv = SDL_GetCurrentAudioDriver();
    if (la->cfg.kind == LIVE_SRC_LOOPBACK && (!drv || strcmp(drv, "pulseaudio") != 0)) {
        /* Monitor sources are listed by SDL's PulseAudio back end (PipeWire
         * serves it through pipewire-pulse); switch to it when another one
         * was chosen and shows none. */
        int monitors = 0;
        for (int i = 0; i < n; i++) monitors += is_monitor(SDL_GetAudioDeviceName(i, 1));
        if (!monitors) {
            if (SDL_AudioInit("pulseaudio") != 0 && SDL_AudioInit(NULL) != 0) {
                fprintf(stderr, "audio: %s\n", SDL_GetError());
                return -1;
            }
            n = SDL_GetNumAudioDevices(1);
        }
    }
    const char **names = n > 0 ? calloc((size_t)n, sizeof *names) : NULL;
    for (int i = 0; i < n && names; i++) names[i] = SDL_GetAudioDeviceName(i, 1);
    if (la->cfg.kind == LIVE_SRC_LOOPBACK) {
        char *sink = NULL;
        SDL_AudioSpec ss;
#if SDL_VERSION_ATLEAST(2, 24, 0)
        if (SDL_GetDefaultAudioInfo(&sink, &ss, 0) != 0) sink = NULL;
#endif
        int k = names ? live_pick_loopback(names, n, sink, la->cfg.device) : -1;
        SDL_free(sink);
        if (k < 0) {
            fprintf(stderr, "loopback: no %s among the capture devices (audio driver %s); "
                            "system audio capture needs PulseAudio or PipeWire for now. "
                            "See: squirrelsound devices\n",
                    la->cfg.device ? "matching device" : "monitor source",
                    SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");
            free(names);
            return -1;
        }
        name = names[k];
    } else if (la->cfg.device && *la->cfg.device) {
        for (int i = 0; i < n && names && !name; i++)
            if (names[i] && contains_ci(names[i], la->cfg.device)) name = names[i];
        if (!name) {
            fprintf(stderr, "mic: no capture device matches \"%s\" (see: squirrelsound devices)\n", la->cfg.device);
            free(names);
            return -1;
        }
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = (int)la->cfg.sample_rate;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    la->cap = SDL_OpenAudioDevice(name, 1, &want, &have, 0); /* SDL converts to what we asked */
    snprintf(la->device, sizeof la->device, "%s", name ? name : "default input");
    free(names);
    if (!la->cap) {
        fprintf(stderr, "%s: cannot open %s: %s\n", live_audio_kind_name(la->cfg.kind), la->device, SDL_GetError());
        return -1;
    }
    return 0;
}
#else
int live_audio_list_devices(FILE *out) {
    (void)out;
    fprintf(stderr, "this build has no SDL2 audio; install libsdl2-dev and rebuild\n");
    return -1;
}
#endif

const char *live_audio_device_name(const LiveAudio *la) { return la ? la->device : ""; }

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
    if (cfg->kind == LIVE_SRC_MIC || cfg->kind == LIVE_SRC_LOOPBACK) {
#ifdef SQSOUND_HAS_SDL
        if (sdl_capture_open(la) != 0) {
            free(la);
            return NULL;
        }
#else
        fprintf(stderr, "%s capture needs SDL2 (install libsdl2-dev and rebuild)\n", live_audio_kind_name(cfg->kind));
        free(la);
        return NULL;
#endif
    }
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
#ifdef SQSOUND_HAS_SDL
    if (la->cap) SDL_CloseAudioDevice(la->cap);
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
#ifdef SQSOUND_HAS_SDL
    if (la->cap) SDL_PauseAudioDevice(la->cap, 0);
#endif
    return 0;
}

void live_audio_stop(LiveAudio *la) {
    if (!la) return;
    la->running = 0;
#ifdef SQSOUND_HAS_ALSA
    if (la->pcm) snd_pcm_drop(la->pcm);
#endif
#ifdef SQSOUND_HAS_SDL
    if (la->cap) SDL_PauseAudioDevice(la->cap, 1);
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

size_t live_audio_take(LiveAudio *la, double *out, size_t max_n, size_t *dropped) {
    if (dropped) *dropped = 0;
    if (!la || !out) return 0;
    unsigned long long pending = la->written - la->taken;
    if (pending > LIVE_RING) {
        if (dropped) *dropped = (size_t)(pending - LIVE_RING);
        la->taken = la->written - LIVE_RING;
        pending = LIVE_RING;
    }
    size_t n = pending < max_n ? (size_t)pending : max_n;
    size_t start = (la->wpos + LIVE_RING - (size_t)pending) % LIVE_RING;
    for (size_t i = 0; i < n; i++) out[i] = la->ring[(start + i) % LIVE_RING];
    la->taken += n;
    return n;
}

void live_audio_take_reset(LiveAudio *la) {
    if (la) la->taken = la->written;
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
#ifdef SQSOUND_HAS_SDL
    if (la->cap) {
        float f[2048];
        Uint32 bytes = SDL_DequeueAudio(la->cap, f, sizeof f);
        size_t got = bytes / sizeof(float);
        for (size_t i = 0; i < got; i++) chunk[i] = f[i];
        ring_write(la, chunk, got);
        return (int)got;
    }
#endif
#ifdef SQSOUND_HAS_SDL
    if (la->cap) {
        float f[2048];
        Uint32 bytes = SDL_DequeueAudio(la->cap, f, sizeof f);
        size_t got = bytes / sizeof(float);
        for (size_t i = 0; i < got; i++) chunk[i] = f[i];
        ring_write(la, chunk, got);
        return (int)got;
    }
#endif
    return 0;
}

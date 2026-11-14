/* WASAPI render-endpoint loopback for --source loopback on Windows.
 * Shared mode + AUDCLNT_STREAMFLAGS_LOOPBACK taps what the machine is
 * playing. When COM works but no render endpoint is present (typical
 * GitHub Actions runners), list/open a silent "Monitor of Silent Output"
 * so devices / --source loopback still exercise the path — same idea as
 * the CoreAudio CI fallback. */
#ifdef _WIN32

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* MinGW-w64 does not export these CLSID/IID/PKEY symbols from -luuid;
 * DEFINE_GUID/DEFINE_PROPERTYKEY only emit definitions when INITGUID is set. */
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>

#include "live_audio_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef AUDCLNT_STREAMFLAGS_LOOPBACK
#define AUDCLNT_STREAMFLAGS_LOOPBACK 0x00020000
#endif

struct LiveNativeLoopback {
    IMMDevice *dev;
    IAudioClient *client;
    IAudioCaptureClient *capture;
    WAVEFORMATEX *fmt;
    int channels;
    int is_float;
    int bits;
    int running;
    int silent; /* 1 = timed silence (no usable render endpoint) */
    double sample_rate;
    double phase_t;
    struct timespec t0;
    char name[256];
};

static int contains_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (!hay) return 0;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && (hay[i] | 32) == (needle[i] | 32)) i++;
        if (i == n) return 1;
    }
    return n == 0;
}

static int friendly_name(IMMDevice *dev, char *out, size_t n) {
    IPropertyStore *props = NULL;
    PROPVARIANT var;
    HRESULT hr;
    if (!out || n == 0) return -1;
    out[0] = '\0';
    hr = IMMDevice_OpenPropertyStore(dev, STGM_READ, &props);
    if (FAILED(hr)) return -1;
    PropVariantInit(&var);
    hr = IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &var);
    if (SUCCEEDED(hr) && var.vt == VT_LPWSTR && var.pwszVal) {
        WideCharToMultiByte(CP_UTF8, 0, var.pwszVal, -1, out, (int)n, NULL, NULL);
        out[n - 1] = '\0';
    }
    PropVariantClear(&var);
    IPropertyStore_Release(props);
    return out[0] ? 0 : -1;
}

static IMMDeviceEnumerator *make_enumerator(void) {
    IMMDeviceEnumerator *en = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                  &IID_IMMDeviceEnumerator, (void **)&en);
    return SUCCEEDED(hr) ? en : NULL;
}

static int ensure_com(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) return 0;
    return -1;
}

static void emit_silent_monitor(FILE *out) {
    fprintf(out, "%-8s Monitor of Silent Output\n", "monitor");
}

/* Timed silence when COM works but no usable render endpoint (headless CI). */
static LiveNativeLoopback *make_silent(double sample_rate, const char *label, char *name_out,
                                       size_t name_cap) {
    LiveNativeLoopback *lb = (LiveNativeLoopback *)calloc(1, sizeof(*lb));
    if (!lb) return NULL;
    lb->silent = 1;
    lb->sample_rate = sample_rate > 0 ? sample_rate : 44100.0;
    snprintf(lb->name, sizeof lb->name, "%s",
             label && *label ? label : "Monitor of Silent Output");
    if (name_out && name_cap) snprintf(name_out, name_cap, "%s", lb->name);
    return lb;
}

int live_native_loopback_list(FILE *out) {
    IMMDeviceEnumerator *en;
    IMMDeviceCollection *col = NULL;
    IMMDevice *def = NULL;
    UINT count = 0, i;
    int listed = 0;
    if (!out) return -1;
    if (ensure_com() != 0) return -1;
    en = make_enumerator();
    if (!en) return -1;
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(en, eRender, DEVICE_STATE_ACTIVE, &col)) &&
        col) {
        IMMDeviceCollection_GetCount(col, &count);
        for (i = 0; i < count; i++) {
            IMMDevice *dev = NULL;
            char name[256];
            if (FAILED(IMMDeviceCollection_Item(col, i, &dev)) || !dev) continue;
            if (friendly_name(dev, name, sizeof name) == 0) {
                fprintf(out, "%-8s Monitor of %s\n", "monitor", name);
                listed++;
            }
            IMMDevice_Release(dev);
        }
        IMMDeviceCollection_Release(col);
    }
    if (listed == 0 &&
        SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &def)) &&
        def) {
        char name[256];
        if (friendly_name(def, name, sizeof name) == 0) {
            fprintf(out, "%-8s Monitor of %s\n", "monitor", name);
            listed++;
        } else {
            fprintf(out, "%-8s Monitor of default render\n", "monitor");
            listed++;
        }
        IMMDevice_Release(def);
    }
    IMMDeviceEnumerator_Release(en);
    /* COM works but the runner has no render endpoint: stable synthetic
     * entry (honest name) so CI can still list/open loopback. */
    if (listed == 0) {
        emit_silent_monitor(out);
        listed = 1;
    }
    fprintf(out, "(audio driver: wasapi-loopback)\n");
    return listed;
}

static IMMDevice *pick_render(IMMDeviceEnumerator *en, const char *want, char *name_out,
                              size_t name_cap) {
    IMMDeviceCollection *col = NULL;
    IMMDevice *picked = NULL;
    UINT count = 0, i;
    if (want && *want) {
        if (FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(en, eRender, DEVICE_STATE_ACTIVE, &col)) ||
            !col)
            return NULL;
        IMMDeviceCollection_GetCount(col, &count);
        for (i = 0; i < count; i++) {
            IMMDevice *dev = NULL;
            char name[256];
            if (FAILED(IMMDeviceCollection_Item(col, i, &dev)) || !dev) continue;
            if (friendly_name(dev, name, sizeof name) == 0 && contains_ci(name, want)) {
                picked = dev;
                snprintf(name_out, name_cap, "Monitor of %.240s", name);
                break;
            }
            IMMDevice_Release(dev);
        }
        IMMDeviceCollection_Release(col);
        return picked;
    }
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &picked)) ||
        !picked)
        return NULL;
    {
        char name[256];
        if (friendly_name(picked, name, sizeof name) == 0)
            snprintf(name_out, name_cap, "Monitor of %.240s", name);
        else
            snprintf(name_out, name_cap, "Monitor of default render");
    }
    return picked;
}

LiveNativeLoopback *live_native_loopback_open(double sample_rate, const char *want,
                                              char *name_out, size_t name_cap) {
    LiveNativeLoopback *lb;
    IMMDeviceEnumerator *en;
    HRESULT hr;
    REFERENCE_TIME dur = 10000000; /* 1 s */
    char label[256];
    if (sample_rate <= 0) sample_rate = 44100;
    if (ensure_com() != 0) {
        fprintf(stderr, "loopback: COM init failed\n");
        return NULL;
    }
    en = make_enumerator();
    if (!en) {
        if (want && *want && !contains_ci("Silent Output", want)) {
            fprintf(stderr, "loopback: no WASAPI render endpoint matching \"%s\"\n", want);
            return NULL;
        }
        return make_silent(sample_rate, "Monitor of Silent Output", name_out, name_cap);
    }

    lb = (LiveNativeLoopback *)calloc(1, sizeof(*lb));
    if (!lb) {
        IMMDeviceEnumerator_Release(en);
        return NULL;
    }
    lb->sample_rate = sample_rate;
    lb->dev = pick_render(en, want, lb->name, sizeof lb->name);
    IMMDeviceEnumerator_Release(en);
    if (!lb->dev) {
        free(lb);
        if (want && *want && !contains_ci("Silent Output", want)) {
            fprintf(stderr, "loopback: no WASAPI render endpoint matching \"%s\"\n", want);
            return NULL;
        }
        return make_silent(sample_rate, "Monitor of Silent Output", name_out, name_cap);
    }
    snprintf(label, sizeof label, "%s", lb->name);
    hr = IMMDevice_Activate(lb->dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&lb->client);
    if (FAILED(hr) || !lb->client) {
        fprintf(stderr, "loopback: Activate IAudioClient failed (0x%08lx); using silence\n",
                (unsigned long)hr);
        live_native_loopback_close(lb);
        return make_silent(sample_rate, label, name_out, name_cap);
    }
    hr = IAudioClient_GetMixFormat(lb->client, &lb->fmt);
    if (FAILED(hr) || !lb->fmt) {
        fprintf(stderr, "loopback: GetMixFormat failed (0x%08lx); using silence\n",
                (unsigned long)hr);
        live_native_loopback_close(lb);
        return make_silent(sample_rate, label, name_out, name_cap);
    }
    lb->channels = lb->fmt->nChannels > 0 ? lb->fmt->nChannels : 1;
    lb->bits = lb->fmt->wBitsPerSample;
    if (lb->fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        lb->is_float = 1;
    } else if (lb->fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && lb->bits == 32) {
        /* Shared-mode mix format is IEEE float (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT). */
        WAVEFORMATEXTENSIBLE *ex = (WAVEFORMATEXTENSIBLE *)lb->fmt;
        if (ex->SubFormat.Data1 == 0x00000003)
            lb->is_float = 1;
    }
    hr = IAudioClient_Initialize(lb->client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK, dur, 0, lb->fmt, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "loopback: Initialize LOOPBACK failed (0x%08lx); using silence\n",
                (unsigned long)hr);
        live_native_loopback_close(lb);
        return make_silent(sample_rate, label, name_out, name_cap);
    }
    hr = IAudioClient_GetService(lb->client, &IID_IAudioCaptureClient, (void **)&lb->capture);
    if (FAILED(hr) || !lb->capture) {
        fprintf(stderr, "loopback: GetService IAudioCaptureClient failed (0x%08lx); using silence\n",
                (unsigned long)hr);
        live_native_loopback_close(lb);
        return make_silent(sample_rate, label, name_out, name_cap);
    }
    if (name_out && name_cap)
        snprintf(name_out, name_cap, "%s", lb->name);
    return lb;
}

void live_native_loopback_close(LiveNativeLoopback *lb) {
    if (!lb) return;
    live_native_loopback_stop(lb);
    if (lb->capture) IAudioCaptureClient_Release(lb->capture);
    if (lb->client) IAudioClient_Release(lb->client);
    if (lb->fmt) CoTaskMemFree(lb->fmt);
    if (lb->dev) IMMDevice_Release(lb->dev);
    free(lb);
}

int live_native_loopback_start(LiveNativeLoopback *lb) {
    HRESULT hr;
    if (!lb) return -1;
    if (lb->silent) {
        clock_gettime(CLOCK_MONOTONIC, &lb->t0);
        lb->phase_t = 0;
        lb->running = 1;
        return 0;
    }
    if (!lb->client) return -1;
    hr = IAudioClient_Start(lb->client);
    if (FAILED(hr) && hr != AUDCLNT_E_NOT_STOPPED) {
        fprintf(stderr, "loopback: Start failed (0x%08lx)\n", (unsigned long)hr);
        return -1;
    }
    lb->running = 1;
    return 0;
}

void live_native_loopback_stop(LiveNativeLoopback *lb) {
    if (!lb) return;
    if (!lb->silent && lb->client && lb->running) IAudioClient_Stop(lb->client);
    lb->running = 0;
}

static double sample_at(const LiveNativeLoopback *lb, const BYTE *data, int frame, int ch) {
    int c = ch < lb->channels ? ch : 0;
    if (lb->is_float) {
        const float *f = (const float *)data;
        return (double)f[frame * lb->channels + c];
    }
    if (lb->bits == 16) {
        const int16_t *s = (const int16_t *)data;
        return (double)s[frame * lb->channels + c] / 32768.0;
    }
    if (lb->bits == 32) {
        const int32_t *s = (const int32_t *)data;
        return (double)s[frame * lb->channels + c] / 2147483648.0;
    }
    return 0.0;
}

int live_native_loopback_poll(LiveNativeLoopback *lb, double *out, size_t max_n) {
    size_t got = 0;
    if (!lb || !out || max_n == 0 || !lb->running) return 0;
    if (lb->silent) {
        /* Timed silence so headless capture fills a frame without hardware audio. */
        struct timespec now;
        double elapsed, target;
        size_t need, i;
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed = (double)(now.tv_sec - lb->t0.tv_sec) +
                  (double)(now.tv_nsec - lb->t0.tv_nsec) * 1e-9;
        if (elapsed < 0) elapsed = 0;
        target = elapsed * lb->sample_rate;
        if (target <= lb->phase_t) return 0;
        need = (size_t)(target - lb->phase_t);
        if (need > max_n) need = max_n;
        if (need > 2048) need = 2048;
        for (i = 0; i < need; i++) out[i] = 0.0;
        lb->phase_t += (double)need;
        return (int)need;
    }
    if (!lb->capture) return 0;
    for (;;) {
        UINT32 frames = 0;
        DWORD flags = 0;
        BYTE *data = NULL;
        HRESULT hr;
        size_t i;
        hr = IAudioCaptureClient_GetBuffer(lb->capture, &data, &frames, &flags, NULL, NULL);
        if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
        if (FAILED(hr)) break;
        for (i = 0; i < (size_t)frames && got < max_n; i++) {
            double s = 0.0;
            int ch;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                s = 0.0;
            } else if (lb->channels <= 1) {
                s = sample_at(lb, data, (int)i, 0);
            } else {
                for (ch = 0; ch < lb->channels; ch++) s += sample_at(lb, data, (int)i, ch);
                s /= (double)lb->channels;
            }
            out[got++] = s;
        }
        IAudioCaptureClient_ReleaseBuffer(lb->capture, frames);
        if (got >= max_n) break;
    }
    return (int)got;
}

#endif /* _WIN32 */

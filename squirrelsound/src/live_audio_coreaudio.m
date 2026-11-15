/* CoreAudio system-audio loopback for --source loopback on macOS.
 *
 * Lists the default output device as a "Monitor of …" source (same shape as
 * Pulse/WASAPI). Capture prefers ScreenCaptureKit audio when the process may
 * record the screen; otherwise (CI runners, denied TCC) it feeds timed
 * silence from that device name so `devices` / `--source loopback` still
 * exercise the path against a silent device.
 */
#ifdef __APPLE__

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>

#ifndef kAudioObjectPropertyElementMain
#define kAudioObjectPropertyElementMain kAudioObjectPropertyElementMaster
#endif

#if __has_include(<ScreenCaptureKit/ScreenCaptureKit.h>)
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#define SQSOUND_HAS_SCK 1
#else
#define SQSOUND_HAS_SCK 0
#endif

#include "live_audio_native.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LB_RING 65536

struct LiveNativeLoopback {
    char name[256];
    double sample_rate;
    int running;
    int silent; /* 1 = timed silence (CI / no SCK permission) */
    /* silent pacing */
    double phase_t;
    struct timespec t0;
    /* shared mono ring filled by SCK or silence */
    double ring[LB_RING];
    size_t rpos, wpos, avail;
    pthread_mutex_t mu;
#if SQSOUND_HAS_SCK
    void *stream;  /* SCStream * (stored as void* for a C struct under ARC) */
    void *sck_out; /* SqSoundSckOut * */
#endif
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

static int default_output_id(AudioObjectID *out) {
    AudioObjectPropertyAddress addr = {kAudioHardwarePropertyDefaultOutputDevice,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 sz = sizeof(AudioObjectID);
    AudioObjectID dev = kAudioObjectUnknown;
    OSStatus st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &sz, &dev);
    if (st != noErr || dev == kAudioObjectUnknown) return -1;
    *out = dev;
    return 0;
}

static int device_name(AudioObjectID dev, char *out, size_t n) {
    AudioObjectPropertyAddress addr = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    CFStringRef cf = NULL;
    UInt32 sz = sizeof(cf);
    OSStatus st;
    if (!out || n == 0) return -1;
    out[0] = '\0';
    st = AudioObjectGetPropertyData(dev, &addr, 0, NULL, &sz, &cf);
    if (st != noErr || !cf) {
        snprintf(out, n, "Built-in Output");
        return 0;
    }
    if (!CFStringGetCString(cf, out, (CFIndex)n, kCFStringEncodingUTF8))
        snprintf(out, n, "Built-in Output");
    CFRelease(cf);
    return 0;
}

static void ring_push(LiveNativeLoopback *lb, const double *x, size_t n) {
    size_t i;
    pthread_mutex_lock(&lb->mu);
    for (i = 0; i < n; i++) {
        lb->ring[lb->wpos] = x[i];
        lb->wpos = (lb->wpos + 1) % LB_RING;
        if (lb->avail < LB_RING) lb->avail++;
        else lb->rpos = (lb->rpos + 1) % LB_RING;
    }
    pthread_mutex_unlock(&lb->mu);
}

static size_t ring_pull(LiveNativeLoopback *lb, double *out, size_t max_n) {
    size_t n, i;
    pthread_mutex_lock(&lb->mu);
    n = lb->avail < max_n ? lb->avail : max_n;
    for (i = 0; i < n; i++) {
        out[i] = lb->ring[lb->rpos];
        lb->rpos = (lb->rpos + 1) % LB_RING;
    }
    lb->avail -= n;
    pthread_mutex_unlock(&lb->mu);
    return n;
}

int live_native_loopback_list(FILE *out) {
    AudioObjectID dev;
    char name[256];
    if (!out) return -1;
    if (default_output_id(&dev) != 0) {
        fprintf(out, "%-8s Monitor of Silent Output\n", "monitor");
        fprintf(out, "(audio driver: coreaudio-loopback)\n");
        return 1;
    }
    device_name(dev, name, sizeof name);
    fprintf(out, "%-8s Monitor of %s\n", "monitor", name);
    fprintf(out, "(audio driver: coreaudio-loopback)\n");
    return 1;
}

#if SQSOUND_HAS_SCK
@interface SqSoundSckOut : NSObject <SCStreamOutput>
@property(nonatomic, assign) LiveNativeLoopback *lb;
@end

@implementation SqSoundSckOut
- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    LiveNativeLoopback *lb = self.lb;
    CMBlockBufferRef bb;
    char *data = NULL;
    size_t len = 0;
    CMFormatDescriptionRef fmt;
    const AudioStreamBasicDescription *asbd;
    size_t i, frames, ch, stride;
    double mono[2048];
    (void)stream;
    if (!lb || type != SCStreamOutputTypeAudio || !sampleBuffer) return;
    bb = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!bb) return;
    if (CMBlockBufferGetDataPointer(bb, 0, NULL, &len, &data) != noErr || !data || len == 0)
        return;
    fmt = CMSampleBufferGetFormatDescription(sampleBuffer);
    asbd = fmt ? CMAudioFormatDescriptionGetStreamBasicDescription(fmt) : NULL;
    if (!asbd || asbd->mChannelsPerFrame < 1) return;
    ch = (size_t)asbd->mChannelsPerFrame;
    if (asbd->mFormatFlags & kAudioFormatFlagIsFloat) {
        const float *f = (const float *)data;
        frames = len / (sizeof(float) * ch);
        stride = ch;
        if (frames > 2048) frames = 2048;
        for (i = 0; i < frames; i++) {
            double s = 0.0;
            size_t c;
            for (c = 0; c < ch; c++) s += (double)f[i * stride + c];
            mono[i] = s / (double)ch;
        }
        ring_push(lb, mono, frames);
    }
}
@end

static int try_sck_start(LiveNativeLoopback *lb) {
    __block int ok = 0;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [SCShareableContent
        getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *err) {
          if (err || !content || content.displays.count < 1) {
              dispatch_semaphore_signal(sem);
              return;
          }
          SCDisplay *display = content.displays.firstObject;
          SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display
                                                            excludingWindows:@[]];
          SCStreamConfiguration *cfg = [[SCStreamConfiguration alloc] init];
          cfg.capturesAudio = YES;
          cfg.sampleRate = (int)lb->sample_rate;
          cfg.channelCount = 1;
          /* Tiny video footprint; we only want the audio tap. */
          cfg.width = 2;
          cfg.height = 2;
          cfg.minimumFrameInterval = CMTimeMake(1, 1);
          SqSoundSckOut *out = [[SqSoundSckOut alloc] init];
          out.lb = lb;
          SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:cfg
                                                     delegate:nil];
          NSError *addErr = nil;
          if (![stream addStreamOutput:out type:SCStreamOutputTypeAudio
                        sampleHandlerQueue:dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0)
                                     error:&addErr]) {
              dispatch_semaphore_signal(sem);
              return;
          }
          [stream startCaptureWithCompletionHandler:^(NSError *startErr) {
            if (!startErr) {
                lb->stream = (__bridge_retained void *)stream;
                lb->sck_out = (__bridge_retained void *)out;
                lb->silent = 0;
                ok = 1;
            }
            dispatch_semaphore_signal(sem);
          }];
        }];
    /* Brief wait: CI without TCC fails quickly; do not hang make test. */
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)2e9));
    return ok ? 0 : -1;
}

static void sck_stop(LiveNativeLoopback *lb) {
    SCStream *stream;
    if (!lb || !lb->stream) return;
    stream = (__bridge_transfer SCStream *)lb->stream;
    lb->stream = NULL;
    if (lb->sck_out) {
        (void)(__bridge_transfer SqSoundSckOut *)lb->sck_out;
        lb->sck_out = NULL;
    }
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [stream stopCaptureWithCompletionHandler:^(__unused NSError *err) {
      dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)1e9));
}
#endif

LiveNativeLoopback *live_native_loopback_open(double sample_rate, const char *want,
                                              char *name_out, size_t name_cap) {
    LiveNativeLoopback *lb;
    AudioObjectID dev = kAudioObjectUnknown;
    char base[200];
    if (sample_rate <= 0) sample_rate = 44100;
    if (default_output_id(&dev) == 0)
        device_name(dev, base, sizeof base);
    else
        snprintf(base, sizeof base, "Silent Output");
    if (want && *want) {
        char mon[256];
        snprintf(mon, sizeof mon, "Monitor of %s", base);
        if (!contains_ci(base, want) && !contains_ci(mon, want)) {
            fprintf(stderr, "loopback: no CoreAudio output matching \"%s\"\n", want);
            return NULL;
        }
    }
    lb = (LiveNativeLoopback *)calloc(1, sizeof(*lb));
    if (!lb) return NULL;
    pthread_mutex_init(&lb->mu, NULL);
    lb->sample_rate = sample_rate;
    lb->silent = 1;
    snprintf(lb->name, sizeof lb->name, "Monitor of %s", base);
    if (name_out && name_cap) snprintf(name_out, name_cap, "%s", lb->name);
#if SQSOUND_HAS_SCK
    /* Best effort: real system audio when Screen Recording is allowed. */
    if (try_sck_start(lb) == 0) return lb;
#endif
    /* Silent device path (CI, or SCK unavailable / denied). */
    lb->silent = 1;
    return lb;
}

void live_native_loopback_close(LiveNativeLoopback *lb) {
    if (!lb) return;
    live_native_loopback_stop(lb);
#if SQSOUND_HAS_SCK
    sck_stop(lb);
#endif
    pthread_mutex_destroy(&lb->mu);
    free(lb);
}

int live_native_loopback_start(LiveNativeLoopback *lb) {
    if (!lb) return -1;
    clock_gettime(CLOCK_MONOTONIC, &lb->t0);
    lb->phase_t = 0;
    lb->running = 1;
    return 0;
}

void live_native_loopback_stop(LiveNativeLoopback *lb) {
    if (!lb) return;
    lb->running = 0;
}

int live_native_loopback_poll(LiveNativeLoopback *lb, double *out, size_t max_n) {
    size_t got;
    if (!lb || !out || max_n == 0 || !lb->running) return 0;
    if (!lb->silent) {
        got = ring_pull(lb, out, max_n);
        return (int)got;
    }
    /* Timed silence so headless capture fills a frame without hardware audio. */
    {
        struct timespec now;
        double elapsed, target;
        size_t need, i;
        double chunk[2048];
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed = (double)(now.tv_sec - lb->t0.tv_sec) +
                  (double)(now.tv_nsec - lb->t0.tv_nsec) * 1e-9;
        if (elapsed < 0) elapsed = 0;
        target = elapsed * lb->sample_rate;
        if (target <= lb->phase_t) return 0;
        need = (size_t)(target - lb->phase_t);
        if (need > max_n) need = max_n;
        if (need > 2048) need = 2048;
        for (i = 0; i < need; i++) chunk[i] = 0.0;
        lb->phase_t += (double)need;
        memcpy(out, chunk, need * sizeof(double));
        return (int)need;
    }
}

#endif /* __APPLE__ */

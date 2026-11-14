/* Native system-audio loopback (WASAPI on Windows, CoreAudio on macOS).
 * Not a public API: live_audio.c calls these when LIVE_SRC_LOOPBACK is
 * opened on those hosts. Linux keeps the SDL Pulse/PipeWire monitor path. */
#ifndef LIVE_AUDIO_NATIVE_H
#define LIVE_AUDIO_NATIVE_H

#include <stddef.h>
#include <stdio.h>

typedef struct LiveNativeLoopback LiveNativeLoopback;

/* Append "monitor …" lines for render endpoints that can be tapped.
 * Returns how many were listed, or -1 on hard failure. */
int live_native_loopback_list(FILE *out);

/* Open the default (or name-matched) loopback. Writes the device label into
 * name_out. Returns NULL on failure. */
LiveNativeLoopback *live_native_loopback_open(double sample_rate, const char *want,
                                              char *name_out, size_t name_cap);

void live_native_loopback_close(LiveNativeLoopback *lb);
int live_native_loopback_start(LiveNativeLoopback *lb);
void live_native_loopback_stop(LiveNativeLoopback *lb);

/* Non-blocking: copy up to max_n mono samples (−1…1) into out. Returns count. */
int live_native_loopback_poll(LiveNativeLoopback *lb, double *out, size_t max_n);

#endif

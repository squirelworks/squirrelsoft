#ifndef LIVE_RECORD_H
#define LIVE_RECORD_H

/* Recording what the live view hears to a 16-bit mono WAV, every sample
 * once (through live_audio_take), for the Rec button and --record. */

#include "live_audio.h"
#include "wavio.h"

typedef struct {
    WavStream *ws;
    char path[1024];
    size_t dropped;  /* samples the tap skipped because the UI fell behind */
} LiveRecorder;

/* Start at "now". path NULL or "" picks squirrelsound-YYYYMMDD-HHMMSS.wav
 * in the current directory. Returns 0, or -1 if the file can't be made. */
int live_record_start(LiveRecorder *r, LiveAudio *audio, const char *path);
/* Move everything the source delivered since the last call into the file */
void live_record_pump(LiveRecorder *r, LiveAudio *audio);
/* Pump the rest, patch the header, close. Returns 0 if all was written. */
int live_record_stop(LiveRecorder *r, LiveAudio *audio);
int live_record_active(const LiveRecorder *r);
double live_record_seconds(const LiveRecorder *r);
/* "REC 0:12 name.wav" for the status line */
void live_record_info(const LiveRecorder *r, char *buf, size_t n);

#endif

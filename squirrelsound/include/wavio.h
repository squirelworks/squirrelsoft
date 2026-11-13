#ifndef WAVIO_H
#define WAVIO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    double *samples; /* mono interleaved as double -1..1 */
    size_t n_samples;
    uint32_t sample_rate;
    uint16_t channels_in_file;
} WavBuffer;

void wav_free(WavBuffer *w);
int wav_read(const char *path, WavBuffer *out);
int wav_write_pcm16(const char *path, const double *samples, size_t n,
                    uint32_t sample_rate);

/* Streaming 16-bit mono writer for recordings of unknown length: the
 * header is patched with the real size on close. */
typedef struct WavStream WavStream;
WavStream *wav_stream_open(const char *path, uint32_t sample_rate);
int wav_stream_write(WavStream *w, const double *samples, size_t n);
double wav_stream_seconds(const WavStream *w);
int wav_stream_close(WavStream *w); /* frees w; 0 if everything was written */

#endif

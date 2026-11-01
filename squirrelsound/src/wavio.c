#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wav_free(WavBuffer *w) {
    free(w->samples);
    w->samples = NULL;
    w->n_samples = 0;
}

static uint32_t rd_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int wav_read(const char *path, WavBuffer *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fclose(f);
        return -1;
    }
    uint16_t audio_format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0, data_bytes = 0;
    long data_pos = 0;
    while (1) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        uint32_t sz = rd_u32(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            unsigned char fmt[40];
            if (sz > sizeof(fmt) || fread(fmt, 1, sz, f) != sz) {
                fclose(f);
                return -1;
            }
            audio_format = rd_u16(fmt);
            channels = rd_u16(fmt + 2);
            sample_rate = rd_u32(fmt + 4);
            bits = rd_u16(fmt + 14);
            if (sz & 1) fgetc(f);
        } else if (!memcmp(ch, "data", 4)) {
            data_pos = ftell(f);
            data_bytes = sz;
            fseek(f, sz + (sz & 1), SEEK_CUR);
        } else {
            fseek(f, sz + (sz & 1), SEEK_CUR);
        }
    }
    if (!data_pos || !sample_rate || !channels ||
        !(audio_format == 1 || audio_format == 3) || !(bits == 16 || bits == 32)) {
        fclose(f);
        return -1;
    }
    size_t frame_bytes = (size_t)channels * (bits / 8);
    size_t nframes = data_bytes / frame_bytes;
    double *mono = (double *)malloc(nframes * sizeof(double));
    if (!mono) {
        fclose(f);
        return -1;
    }
    fseek(f, data_pos, SEEK_SET);
    if (audio_format == 1 && bits == 16) {
        for (size_t i = 0; i < nframes; i++) {
            int16_t s = 0;
            for (uint16_t c = 0; c < channels; c++) {
                unsigned char b[2];
                if (fread(b, 1, 2, f) != 2) {
                    free(mono);
                    fclose(f);
                    return -1;
                }
                if (c == 0) s = (int16_t)(b[0] | (b[1] << 8));
            }
            mono[i] = (double)s / 32768.0;
        }
    } else if (audio_format == 3 && bits == 32) {
        for (size_t i = 0; i < nframes; i++) {
            float s = 0;
            for (uint16_t c = 0; c < channels; c++) {
                float v;
                if (fread(&v, 4, 1, f) != 1) {
                    free(mono);
                    fclose(f);
                    return -1;
                }
                if (c == 0) s = v;
            }
            mono[i] = (double)s;
        }
    } else {
        free(mono);
        fclose(f);
        return -1;
    }
    fclose(f);
    out->samples = mono;
    out->n_samples = nframes;
    out->sample_rate = sample_rate;
    out->channels_in_file = channels;
    return 0;
}

static void wr_u32(FILE *f, uint32_t v) {
    unsigned char b[4] = {v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255};
    fwrite(b, 1, 4, f);
}
static void wr_u16(FILE *f, uint16_t v) {
    unsigned char b[2] = {v & 255, (v >> 8) & 255};
    fwrite(b, 1, 2, f);
}

int wav_write_pcm16(const char *path, const double *samples, size_t n,
                    uint32_t sample_rate) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data_bytes = (uint32_t)(n * 2);
    fwrite("RIFF", 1, 4, f);
    wr_u32(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    wr_u32(f, 16);
    wr_u16(f, 1);
    wr_u16(f, 1);
    wr_u32(f, sample_rate);
    wr_u32(f, sample_rate * 2);
    wr_u16(f, 2);
    wr_u16(f, 16);
    fwrite("data", 1, 4, f);
    wr_u32(f, data_bytes);
    for (size_t i = 0; i < n; i++) {
        double v = samples[i];
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        int16_t s = (int16_t)lrint(v * 32767.0);
        unsigned char b[2] = {(unsigned char)(s & 255), (unsigned char)((s >> 8) & 255)};
        fwrite(b, 1, 2, f);
    }
    fclose(f);
    return 0;
}

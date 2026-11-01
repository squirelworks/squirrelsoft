#include "live_audio.h"
#include "live_engine.h"
#include "live_view.h"
#include "spectrogram.h"
#include "sqdsp.h"
#include "synth.h"
#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    puts("SquirrelSound — spectral analyzer (sqdsp)");
    puts("  squirrelsound synth <out.wav> [--freq Hz] [--seconds s] [--rate Hz]");
    puts("                      [--harmonics N] [--amp A] [--noise A]");
    puts("  squirrelsound spectrum <in.wav> [--fft N] [--window name]");
    puts("  squirrelsound welch <in.wav> [--seg N] [--overlap F] [--window name]");
    puts("  squirrelsound pitch <in.wav> [--fmin Hz] [--fmax Hz]");
    puts("  squirrelsound thd <in.wav> [--fund Hz] [--harmonics N] [--fft N]");
    puts("  squirrelsound snr <in.wav> [--fund Hz] [--bw Hz] [--fft N]");
    puts("  squirrelsound spectrogram <in.wav> <out.svg> [--fft N] [--overlap F]");
    puts("  squirrelsound live [--source demo|wav|alsa] [--file path] [--device name]");
    puts("                     [--fft N] [--window name] [--freq Hz] [--headless]");
    puts("                     [--frames N] [--max-hold]");
    puts("  squirrelsound help");
    puts("Phone field UI (mic + QR + Suica): see ../squirrelfield/web/");
}

static const char *get_opt(int argc, char **argv, const char *key, const char *def) {
    size_t n = strlen(key);
    for (int i = 0; i < argc - 1; i++) {
        if (strncmp(argv[i], key, n) == 0 && argv[i][n] == '\0') return argv[i + 1];
    }
    return def;
}

static int has_flag(int argc, char **argv, const char *key) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], key) == 0) return 1;
    return 0;
}

static size_t choose_fft(size_t n, size_t want) {
    if (want && sqdsp_is_pow2(want) && want <= n) return want;
    size_t p = 1;
    while ((p << 1) <= n && p < 16384) p <<= 1;
    if (p < 256) p = n >= 256 ? 256 : (sqdsp_is_pow2(n) ? n : 128);
    while (p > n) p >>= 1;
    return p ? p : 1;
}

static void print_top_peaks(const double *mag, const double *freq, size_t nb, int topn) {
    int idx[32];
    int n = topn > 32 ? 32 : topn;
    for (int i = 0; i < n; i++) idx[i] = -1;
    for (size_t k = 1; k + 1 < nb; k++) {
        if (!(mag[k] >= mag[k - 1] && mag[k] >= mag[k + 1])) continue;
        for (int i = 0; i < n; i++) {
            if (idx[i] < 0 || mag[k] > mag[idx[i]]) {
                for (int j = n - 1; j > i; j--) idx[j] = idx[j - 1];
                idx[i] = (int)k;
                break;
            }
        }
    }
    puts("top peaks:");
    for (int i = 0; i < n && idx[i] >= 0; i++) {
        double bin = sqdsp_parabolic_bin(mag, nb, (size_t)idx[i]);
        size_t i0 = (size_t)bin;
        double frac = bin - (double)i0;
        double f =
            (i0 + 1 < nb) ? freq[i0] * (1.0 - frac) + freq[i0 + 1] * frac : freq[i0];
        printf("  %2d: %10.3f Hz   mag=%.6f\n", i + 1, f, mag[idx[i]]);
    }
}

int main(int argc, char **argv) {
    if (argc < 2 || has_flag(argc, argv, "help") || has_flag(argc, argv, "--help")) {
        usage();
        return argc < 2 ? 1 : 0;
    }
    const char *cmd = argv[1];

    if (strcmp(cmd, "synth") == 0) {
        if (argc < 3) {
            usage();
            return 1;
        }
        double freq = atof(get_opt(argc, argv, "--freq", "440"));
        double seconds = atof(get_opt(argc, argv, "--seconds", "1.0"));
        double rate = atof(get_opt(argc, argv, "--rate", "44100"));
        int harmonics = atoi(get_opt(argc, argv, "--harmonics", "1"));
        double amp = atof(get_opt(argc, argv, "--amp", "0.4"));
        double noise = atof(get_opt(argc, argv, "--noise", "0"));
        size_t n = (size_t)(seconds * rate);
        double *buf = (double *)calloc(n, sizeof(double));
        if (!buf) return 1;
        synth_tone(buf, n, rate, freq, harmonics, amp);
        if (noise > 0.0) {
            double *nz = (double *)malloc(n * sizeof(double));
            synth_noise(nz, n, noise, 42);
            for (size_t i = 0; i < n; i++) buf[i] += nz[i];
            free(nz);
        }
        int rc = wav_write_pcm16(argv[2], buf, n, (uint32_t)rate);
        free(buf);
        if (rc != 0) {
            fprintf(stderr, "write failed\n");
            return 1;
        }
        printf("wrote %s  (%zu samples @ %.0f Hz)\n", argv[2], n, rate);
        return 0;
    }

    if (strcmp(cmd, "spectrum") == 0 || strcmp(cmd, "welch") == 0 ||
        strcmp(cmd, "pitch") == 0 || strcmp(cmd, "thd") == 0 || strcmp(cmd, "snr") == 0 ||
        strcmp(cmd, "spectrogram") == 0) {
        if (argc < 3) {
            usage();
            return 1;
        }
        WavBuffer wav;
        if (wav_read(argv[2], &wav) != 0) {
            fprintf(stderr, "cannot read wav %s\n", argv[2]);
            return 1;
        }
        double fs = (double)wav.sample_rate;
        SqdspWindow win = sqdsp_window_parse(get_opt(argc, argv, "--window", "hann"));

        if (strcmp(cmd, "spectrum") == 0) {
            size_t want = (size_t)atoi(get_opt(argc, argv, "--fft", "0"));
            size_t nfft = choose_fft(wav.n_samples, want);
            double *mag = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *freq = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *psd = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *scratch = (double *)malloc(nfft * 3 * sizeof(double));
            size_t nb =
                sqdsp_periodogram(wav.samples, nfft, fs, win, psd, freq, scratch);
            for (size_t i = 0; i < nb; i++) mag[i] = sqrt(psd[i] * fs); /* display scale */
            printf("fft=%zu  fs=%.1f  window=%s  bins=%zu\n", nfft, fs,
                   get_opt(argc, argv, "--window", "hann"), nb);
            print_top_peaks(mag, freq, nb, 8);
            double peak = sqdsp_peak_frequency(mag, freq, nb);
            printf("dominant ~= %.3f Hz\n", peak);
            free(mag);
            free(freq);
            free(psd);
            free(scratch);
        } else if (strcmp(cmd, "welch") == 0) {
            size_t seg = (size_t)atoi(get_opt(argc, argv, "--seg", "2048"));
            if (!sqdsp_is_pow2(seg)) seg = 2048;
            if (seg > wav.n_samples) seg = choose_fft(wav.n_samples, 0);
            double overlap = atof(get_opt(argc, argv, "--overlap", "0.5"));
            size_t nb = seg / 2 + 1;
            double *psd = (double *)malloc(nb * sizeof(double));
            double *freq = (double *)malloc(nb * sizeof(double));
            double *scratch = (double *)malloc((nb * 2 + seg * 3) * sizeof(double));
            size_t got = sqdsp_welch_psd(wav.samples, wav.n_samples, fs, seg, overlap, win,
                                        psd, freq, scratch);
            printf("welch seg=%zu overlap=%.2f bins=%zu\n", seg, overlap, got);
            double *mag = (double *)malloc(got * sizeof(double));
            for (size_t i = 0; i < got; i++) mag[i] = psd[i];
            print_top_peaks(mag, freq, got, 8);
            free(mag);
            free(psd);
            free(freq);
            free(scratch);
        } else if (strcmp(cmd, "pitch") == 0) {
            double fmin = atof(get_opt(argc, argv, "--fmin", "60"));
            double fmax = atof(get_opt(argc, argv, "--fmax", "1000"));
            size_t n = wav.n_samples > 8192 ? 8192 : wav.n_samples;
            size_t np2 = 1;
            while (np2 < n) np2 <<= 1;
            np2 <<= 1;
            double *scratch = (double *)malloc((n + np2 + 8) * sizeof(double));
            double f0 =
                sqdsp_pitch_autocorr(wav.samples, n, fs, fmin, fmax, scratch);
            printf("pitch (autocorr) ~= %.3f Hz\n", f0);
            size_t nfft = choose_fft(wav.n_samples, 4096);
            double *psd = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *freq = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *tmp = (double *)malloc(nfft * 3 * sizeof(double));
            size_t nb = sqdsp_periodogram(wav.samples, nfft, fs, win, psd, freq, tmp);
            double *mag = psd;
            for (size_t i = 0; i < nb; i++) mag[i] = sqrt(psd[i]);
            printf("pitch (spectrum peak) ~= %.3f Hz\n",
                   sqdsp_peak_frequency(mag, freq, nb));
            free(scratch);
            free(psd);
            free(freq);
            free(tmp);
        } else if (strcmp(cmd, "thd") == 0) {
            size_t nfft = choose_fft(wav.n_samples,
                                     (size_t)atoi(get_opt(argc, argv, "--fft", "8192")));
            int harms = atoi(get_opt(argc, argv, "--harmonics", "5"));
            double *psd = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *freq = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *tmp = (double *)malloc(nfft * 3 * sizeof(double));
            size_t nb = sqdsp_periodogram(wav.samples, nfft, fs, win, psd, freq, tmp);
            double *mag = (double *)malloc(nb * sizeof(double));
            for (size_t i = 0; i < nb; i++) mag[i] = sqrt(psd[i]);
            double fund = atof(get_opt(argc, argv, "--fund", "0"));
            if (fund <= 0.0) fund = sqdsp_peak_frequency(mag, freq, nb);
            double thd = sqdsp_thd(mag, freq, nb, fund, harms);
            printf("fund=%.3f Hz  THD=%.6f (%.2f%%)\n", fund, thd, thd * 100.0);
            free(psd);
            free(freq);
            free(tmp);
            free(mag);
        } else if (strcmp(cmd, "snr") == 0) {
            size_t nfft = choose_fft(wav.n_samples,
                                     (size_t)atoi(get_opt(argc, argv, "--fft", "8192")));
            double *psd = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *freq = (double *)malloc((nfft / 2 + 1) * sizeof(double));
            double *tmp = (double *)malloc(nfft * 3 * sizeof(double));
            size_t nb = sqdsp_periodogram(wav.samples, nfft, fs, win, psd, freq, tmp);
            double fund = atof(get_opt(argc, argv, "--fund", "0"));
            double bw = atof(get_opt(argc, argv, "--bw", "20"));
            if (fund <= 0.0) {
                double *mag = (double *)malloc(nb * sizeof(double));
                for (size_t i = 0; i < nb; i++) mag[i] = sqrt(psd[i]);
                fund = sqdsp_peak_frequency(mag, freq, nb);
                free(mag);
            }
            double snr = sqdsp_snr_db(psd, freq, nb, fund, bw);
            printf("fund=%.3f Hz  bw=±%.1f Hz  SNR=%.2f dB\n", fund, bw, snr);
            free(psd);
            free(freq);
            free(tmp);
        } else if (strcmp(cmd, "spectrogram") == 0) {
            if (argc < 4) {
                usage();
                wav_free(&wav);
                return 1;
            }
            size_t nfft = choose_fft(wav.n_samples,
                                     (size_t)atoi(get_opt(argc, argv, "--fft", "1024")));
            double overlap = atof(get_opt(argc, argv, "--overlap", "0.75"));
            if (sqsound_write_spectrogram_svg(argv[3], wav.samples, wav.n_samples, fs,
                                              nfft, overlap, win) != 0) {
                fprintf(stderr, "spectrogram failed\n");
                wav_free(&wav);
                return 1;
            }
            printf("wrote %s\n", argv[3]);
        }
        wav_free(&wav);
        return 0;
    }

    if (strcmp(cmd, "live") == 0) {
        const char *src = get_opt(argc, argv, "--source", "demo");
        LiveAudioConfig acfg;
        memset(&acfg, 0, sizeof(acfg));
        acfg.sample_rate = atof(get_opt(argc, argv, "--rate", "44100"));
        acfg.demo_freq_hz = atof(get_opt(argc, argv, "--freq", "440"));
        acfg.demo_harmonics = atoi(get_opt(argc, argv, "--harmonics", "3"));
        acfg.wav_path = get_opt(argc, argv, "--file", NULL);
        acfg.alsa_dev = get_opt(argc, argv, "--device", "default");
        if (strcmp(src, "wav") == 0) acfg.kind = LIVE_SRC_WAV;
        else if (strcmp(src, "alsa") == 0 || strcmp(src, "mic") == 0)
            acfg.kind = LIVE_SRC_ALSA;
        else acfg.kind = LIVE_SRC_DEMO;

        LiveAudio *audio = live_audio_open(&acfg);
        if (!audio) {
            fprintf(stderr, "failed to open live source '%s'\n", src);
            return 1;
        }

        LiveEngineConfig ecfg;
        memset(&ecfg, 0, sizeof(ecfg));
        ecfg.fft_size = (size_t)atoi(get_opt(argc, argv, "--fft", "2048"));
        if (!sqdsp_is_pow2(ecfg.fft_size)) ecfg.fft_size = 2048;
        ecfg.window = sqdsp_window_parse(get_opt(argc, argv, "--window", "hann"));
        ecfg.sample_rate = live_audio_sample_rate(audio);
        ecfg.max_hold = has_flag(argc, argv, "--max-hold");
        ecfg.db_min = -90;
        ecfg.db_max = 0;
        ecfg.f_min_hz = 0;
        ecfg.f_max_hz = ecfg.sample_rate * 0.5;

        LiveEngine engine;
        if (live_engine_init(&engine, &ecfg) != 0) {
            fprintf(stderr, "live engine init failed\n");
            live_audio_close(audio);
            return 1;
        }

        int rc;
        if (has_flag(argc, argv, "--headless")) {
            int frames = atoi(get_opt(argc, argv, "--frames", "30"));
            rc = live_view_headless_run(audio, &engine, frames, 5);
        } else {
            rc = live_view_sdl_run(audio, &engine);
        }
        live_engine_free(&engine);
        live_audio_close(audio);
        return rc == 0 ? 0 : 1;
    }

    usage();
    return 1;
}

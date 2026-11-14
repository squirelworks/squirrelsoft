/*
 * JNI bridge: SquirrelSound -> libs/sqdsp.
 *
 * nativeSpectrumDb(windowed, windowSum, floorDb, outDb) is the native twin of
 * Fft.forward + Fft.magnitudeDb in Fft.kt: a one-sided magnitude spectrum of
 * an already-windowed power-of-two frame, scaled by 2/windowSum (DC and
 * Nyquist by 1/windowSum) so a full-scale sine reads 0 dBFS, in dB, clamped
 * at floorDb. Keep the scaling in step with Fft.magnitudeDb.
 *
 * nativeOctaveBands(fraction, fmin, fmax, centre, lo, hi) writes the IEC
 * 61260-1 base-10 fractional-octave bands whose centre lies in [fmin, fmax]
 * (as many as the arrays hold) and returns how many there are in all.
 *
 * nativeBandLevels(magDb, binHz, lo, hi, nbands, enbw, floorDb, outDb) sums
 * the power of the bins in each band (bin k is at k * binHz) and divides by
 * the window's ENBW, so a pure tone reads its own level. Bands.kt is the
 * Kotlin twin of both; keep them in step.
 *
 * nativePeaks(magDb, sampleRate, fftSize, fMin, fMax, dbMin, maxN, minSepHz,
 * outHz, outDb) finds the loudest local maxima via sqdsp_local_peaks — the
 * same routine live_engine_peaks uses on the desktop. Pass the display /
 * config floor as dbMin; this JNI entry adds +10 dB (matching
 * live_engine_peaks' cfg.db_min + 10) before the threshold check.
 */
#include <jni.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sqdsp.h"

JNIEXPORT jboolean JNICALL
Java_works_squirrel_sound_SqDsp_nativeSpectrumDb(JNIEnv *env, jclass cls, jfloatArray windowed, jfloat window_sum,
                                                 jfloat floor_db, jfloatArray out_db) {
    (void)cls;
    if (!windowed || !out_db || !(window_sum > 0.0f)) return JNI_FALSE;
    jsize n = (*env)->GetArrayLength(env, windowed), m = (*env)->GetArrayLength(env, out_db);
    if (n < 2 || !sqdsp_is_pow2((size_t)n) || m < 1 || m > n / 2 + 1) return JNI_FALSE;
    size_t half = (size_t)n / 2;
    double *x = malloc(sizeof(double) * (size_t)n), *scratch = malloc(sizeof(double) * (size_t)n);
    double *mag = malloc(sizeof(double) * (half + 1));
    jfloat *in = NULL, *out = NULL;
    jboolean ok = JNI_FALSE;
    if (!x || !scratch || !mag) goto done;
    in = (*env)->GetFloatArrayElements(env, windowed, NULL);
    if (!in) goto done;
    for (jsize i = 0; i < n; i++) x[i] = in[i];
    (*env)->ReleaseFloatArrayElements(env, windowed, in, JNI_ABORT);
    if (sqdsp_magnitude_spectrum(x, (size_t)n, mag, scratch) != half + 1) goto done;
    out = (*env)->GetFloatArrayElements(env, out_db, NULL);
    if (!out) goto done;
    for (jsize k = 0; k < m; k++) {
        double scale = (k == 0 || (size_t)k == half) ? 1.0 / window_sum : 2.0 / window_sum;
        double v = mag[k] * scale;
        float db = v > 1e-12 ? (float)(20.0 * log10(v)) : floor_db;
        out[k] = db < floor_db ? floor_db : db;
    }
    (*env)->ReleaseFloatArrayElements(env, out_db, out, 0);
    ok = JNI_TRUE;
done:
    free(x);
    free(scratch);
    free(mag);
    return ok;
}

JNIEXPORT jint JNICALL
Java_works_squirrel_sound_SqDsp_nativeOctaveBands(JNIEnv *env, jclass cls, jint fraction, jdouble fmin, jdouble fmax,
                                                  jdoubleArray centre, jdoubleArray lo, jdoubleArray hi) {
    (void)cls;
    if (!centre || !lo || !hi) return -1;
    jsize cap = (*env)->GetArrayLength(env, centre);
    if ((*env)->GetArrayLength(env, lo) < cap) cap = (*env)->GetArrayLength(env, lo);
    if ((*env)->GetArrayLength(env, hi) < cap) cap = (*env)->GetArrayLength(env, hi);
    size_t total = sqdsp_octave_bands(fraction, fmin, fmax, NULL, NULL, NULL, 0);
    size_t n = total < (size_t)cap ? total : (size_t)cap;
    if (n == 0) return (jint)total;
    double *c = malloc(sizeof(double) * total), *l = malloc(sizeof(double) * total), *h = malloc(sizeof(double) * total);
    if (c && l && h) {
        sqdsp_octave_bands(fraction, fmin, fmax, c, l, h, total);
        (*env)->SetDoubleArrayRegion(env, centre, 0, (jsize)n, c);
        (*env)->SetDoubleArrayRegion(env, lo, 0, (jsize)n, l);
        (*env)->SetDoubleArrayRegion(env, hi, 0, (jsize)n, h);
    } else {
        total = 0;
    }
    free(c);
    free(l);
    free(h);
    return (jint)total;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_sound_SqDsp_nativeBandLevels(JNIEnv *env, jclass cls, jfloatArray mag_db, jdouble bin_hz,
                                                 jdoubleArray lo, jdoubleArray hi, jint nbands, jdouble enbw,
                                                 jdouble floor_db, jfloatArray out_db) {
    (void)cls;
    if (!mag_db || !lo || !hi || !out_db || nbands < 0 || !(bin_hz > 0)) return JNI_FALSE;
    jsize nbins = (*env)->GetArrayLength(env, mag_db);
    if ((*env)->GetArrayLength(env, lo) < nbands || (*env)->GetArrayLength(env, hi) < nbands ||
        (*env)->GetArrayLength(env, out_db) < nbands)
        return JNI_FALSE;
    if (nbands == 0) return JNI_TRUE;
    size_t nb = (size_t)nbins, bands = (size_t)nbands;
    double *mag = malloc(sizeof(double) * (nb ? nb : 1)), *freq = malloc(sizeof(double) * (nb ? nb : 1));
    double *l = malloc(sizeof(double) * bands), *h = malloc(sizeof(double) * bands);
    double *out = malloc(sizeof(double) * bands);
    jfloat *m = NULL, *o = NULL;
    jboolean ok = JNI_FALSE;
    if (!mag || !freq || !l || !h || !out) goto done;
    m = (*env)->GetFloatArrayElements(env, mag_db, NULL);
    if (!m) goto done;
    for (size_t k = 0; k < nb; k++) {
        mag[k] = m[k];
        freq[k] = (double)k * bin_hz;
    }
    (*env)->ReleaseFloatArrayElements(env, mag_db, m, JNI_ABORT);
    (*env)->GetDoubleArrayRegion(env, lo, 0, nbands, l);
    (*env)->GetDoubleArrayRegion(env, hi, 0, nbands, h);
    sqdsp_band_levels_db(mag, freq, nb, l, h, bands, enbw, floor_db, out);
    o = (*env)->GetFloatArrayElements(env, out_db, NULL);
    if (!o) goto done;
    for (size_t j = 0; j < bands; j++) o[j] = (jfloat)out[j];
    (*env)->ReleaseFloatArrayElements(env, out_db, o, 0);
    ok = JNI_TRUE;
done:
    free(mag);
    free(freq);
    free(l);
    free(h);
    free(out);
    return ok;
}

JNIEXPORT jint JNICALL
Java_works_squirrel_sound_SqDsp_nativePeaks(JNIEnv *env, jclass cls, jfloatArray mag_db,
                                            jdouble sample_rate, jint fft_size, jdouble f_min,
                                            jdouble f_max, jdouble db_min, jint max_n,
                                            jdouble min_sep_hz, jdoubleArray out_hz,
                                            jdoubleArray out_db) {
    (void)cls;
    if (!mag_db || !out_hz || !out_db || max_n <= 0 || !(sample_rate > 0) || fft_size <= 0)
        return 0;
    jsize nbins = (*env)->GetArrayLength(env, mag_db);
    if (nbins < 3) return 0;
    jsize cap = (*env)->GetArrayLength(env, out_hz);
    if ((*env)->GetArrayLength(env, out_db) < cap) cap = (*env)->GetArrayLength(env, out_db);
    if (cap <= 0) return 0;
    size_t want = (size_t)max_n < (size_t)cap ? (size_t)max_n : (size_t)cap;
    double *mag = malloc(sizeof(double) * (size_t)nbins);
    double *hz = malloc(sizeof(double) * want);
    double *db = malloc(sizeof(double) * want);
    jfloat *m = NULL;
    jint n = 0;
    if (!mag || !hz || !db) goto done;
    m = (*env)->GetFloatArrayElements(env, mag_db, NULL);
    if (!m) goto done;
    for (jsize k = 0; k < nbins; k++) mag[k] = m[k];
    (*env)->ReleaseFloatArrayElements(env, mag_db, m, JNI_ABORT);
    /* bin k at k * fs / fft_size — same rounding as live_engine_hz_to_bin */
    size_t b0 = 1, b1 = (size_t)nbins - 1;
    if (f_min > 0) {
        double b = f_min * (double)fft_size / sample_rate;
        if (b < 0) b = 0;
        if (b > (double)(nbins - 1)) b = (double)(nbins - 1);
        b0 = (size_t)(b + 0.5);
    }
    if (f_max > 0) {
        double b = f_max * (double)fft_size / sample_rate;
        if (b < 0) b = 0;
        if (b > (double)(nbins - 1)) b = (double)(nbins - 1);
        b1 = (size_t)(b + 0.5);
    }
    n = (jint)sqdsp_local_peaks(mag, (size_t)nbins, sample_rate, (size_t)fft_size, b0, b1,
                                db_min + 10.0, want, min_sep_hz, hz, db);
    if (n > 0) {
        (*env)->SetDoubleArrayRegion(env, out_hz, 0, n, hz);
        (*env)->SetDoubleArrayRegion(env, out_db, 0, n, db);
    }
done:
    free(mag);
    free(hz);
    free(db);
    return n;
}

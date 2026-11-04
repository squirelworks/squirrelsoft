/*
 * JNI bridge: SquirrelSound -> libs/sqdsp.
 *
 * nativeSpectrumDb(windowed, windowSum, floorDb, outDb) is the native twin of
 * Fft.forward + Fft.magnitudeDb in Fft.kt: a one-sided magnitude spectrum of
 * an already-windowed power-of-two frame, scaled by 2/windowSum (DC and
 * Nyquist by 1/windowSum) so a full-scale sine reads 0 dBFS, in dB, clamped
 * at floorDb. Keep the scaling in step with Fft.magnitudeDb.
 */
#include <jni.h>
#include <math.h>
#include <stdlib.h>

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

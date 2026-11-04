/*
 * JNI bridge: SquirrelQR -> libs/sqqr.
 *
 * nativeDecode(gray, width, height, stride, meta) finds and decodes one QR
 * symbol in an 8-bit luminance image (the camera's Y plane). It returns the
 * payload bytes, or null. On success meta (at least META_LEN doubles) holds:
 *   [0..7] outer corners x,y: top-left, top-right, bottom-right, bottom-left
 *   [8] ECI assignment number or -1, [9] 1 if Kanji mode was used,
 *   [10] version, [11] codewords corrected.
 * SqQr.kt reads them in this order; keep the two in step.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "sqqr.h"

#define META_LEN 12

JNIEXPORT jbyteArray JNICALL
Java_works_squirrel_qr_SqQr_nativeDecode(JNIEnv *env, jclass cls, jbyteArray gray, jint w, jint h,
                                         jint stride, jdoubleArray meta) {
    (void)cls;
    if (!gray || !meta || w < 21 || h < 21 || stride < w) return NULL;
    if ((*env)->GetArrayLength(env, meta) < META_LEN) return NULL;
    if ((*env)->GetArrayLength(env, gray) < (jsize)((size_t)stride * (size_t)(h - 1) + (size_t)w)) return NULL;
    size_t work_size = SQQR_IMAGE_WORK(w, h);
    void *work = malloc(work_size);
    SqqrResult *res = malloc(sizeof *res);
    jbyteArray out = NULL;
    if (work && res) {
        jbyte *px = (*env)->GetByteArrayElements(env, gray, NULL);
        if (px) {
            int rc = sqqr_decode_image((const uint8_t *)px, w, h, stride, work, work_size, res);
            (*env)->ReleaseByteArrayElements(env, gray, px, JNI_ABORT);
            if (rc == SQQR_OK) {
                jdouble m[META_LEN];
                for (int i = 0; i < 8; i++) m[i] = res->corners[i];
                m[8] = res->eci;
                m[9] = res->has_kanji;
                m[10] = res->version;
                m[11] = res->corrected;
                (*env)->SetDoubleArrayRegion(env, meta, 0, META_LEN, m);
                out = (*env)->NewByteArray(env, (jsize)res->len);
                if (out) (*env)->SetByteArrayRegion(env, out, 0, (jsize)res->len, (const jbyte *)res->data);
            }
        }
    }
    free(work);
    free(res);
    return out;
}

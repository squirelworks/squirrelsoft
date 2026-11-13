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
 *
 * nativeEncode(data, level, info) makes a symbol (automatic version, mode and
 * mask): the modules row by row, 1 = dark, or null with info[0] set to the
 * SQQR_ERR_* code. On success info (at least INFO_LEN ints) holds
 *   [0] 0, [1] version, [2] size in modules, [3] mask, [4] mode.
 *
 * nativeContent(utf8) reads what a payload means (libs/sqqr sqqr_content):
 * the UTF-8 strings type id, type name, then label and value pairs, each
 * ended by a NUL byte. Text crosses as bytes, not jstring, so characters
 * outside the BMP need no modified-UTF-8 care.
 *
 * nativeWifi(ssid, password, security, hidden) builds a WIFI: payload
 * (strings as UTF-8 bytes; security may be empty).
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "sqqr.h"
#include "sqqr_content.h"

#define META_LEN 12
#define INFO_LEN 5

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

JNIEXPORT jbyteArray JNICALL
Java_works_squirrel_qr_SqQr_nativeEncode(JNIEnv *env, jclass cls, jbyteArray data, jint level, jintArray info) {
    (void)cls;
    if (!data || !info || level < SQQR_EC_L || level > SQQR_EC_H) return NULL;
    if ((*env)->GetArrayLength(env, info) < INFO_LEN) return NULL;
    jsize len = (*env)->GetArrayLength(env, data);
    size_t cap = (size_t)SQQR_MAX_SIZE * SQQR_MAX_SIZE;
    uint8_t *grid = malloc(cap);
    jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
    jbyteArray out = NULL;
    jint m[INFO_LEN] = {SQQR_ERR_SIZE, 0, 0, 0, 0};
    if (grid && bytes) {
        SqqrEncodeOptions o = {(SqqrEcLevel)level, 0, -1, -1, SQQR_MODE_AUTO};
        SqqrEncodeInfo ei;
        int rc = sqqr_encode((const uint8_t *)bytes, (size_t)len, &o, grid, cap, &ei);
        m[0] = rc;
        if (rc == SQQR_OK) {
            m[1] = ei.version;
            m[2] = ei.size;
            m[3] = ei.mask;
            m[4] = ei.mode;
            jsize n = (jsize)(ei.size * ei.size);
            out = (*env)->NewByteArray(env, n);
            if (out) (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)grid);
        }
    }
    if (bytes) (*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
    (*env)->SetIntArrayRegion(env, info, 0, INFO_LEN, m);
    free(grid);
    return out;
}

/* a Java byte array as a NUL-terminated C string (NULL on failure; free it) */
static char *cstr(JNIEnv *env, jbyteArray a) {
    jsize n = a ? (*env)->GetArrayLength(env, a) : 0;
    char *s = malloc((size_t)n + 1);
    if (!s) return NULL;
    if (n > 0) (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *)s);
    s[n] = 0;
    return s;
}

static jbyteArray bytes_of(JNIEnv *env, const char *s, size_t n) {
    jbyteArray out = (*env)->NewByteArray(env, (jsize)n);
    if (out && n) (*env)->SetByteArrayRegion(env, out, 0, (jsize)n, (const jbyte *)s);
    return out;
}

static size_t append(char *buf, size_t at, size_t cap, const char *s) {
    size_t n = strlen(s);
    if (at + n + 1 > cap) return at;
    memcpy(buf + at, s, n + 1); /* keeps the NUL as the separator */
    return at + n + 1;
}

JNIEXPORT jbyteArray JNICALL
Java_works_squirrel_qr_SqQr_nativeContent(JNIEnv *env, jclass cls, jbyteArray utf8) {
    (void)cls;
    char *text = cstr(env, utf8);
    SqqrContent *c = malloc(sizeof *c);
    size_t cap = sizeof c->buf + 64 * SQQR_CONTENT_MAX_FIELDS + 64;
    char *buf = malloc(cap);
    jbyteArray out = NULL;
    if (text && c && buf) {
        sqqr_content_parse(text, c);
        size_t at = 0;
        at = append(buf, at, cap, sqqr_content_type_id(c->type));
        at = append(buf, at, cap, sqqr_content_type_name(c->type));
        for (int i = 0; i < c->nfields; i++) {
            size_t before = at;
            at = append(buf, at, cap, c->field[i].label);
            size_t mid = at;
            at = append(buf, at, cap, c->field[i].value);
            if (at == mid) at = before; /* never a label without its value */
        }
        out = bytes_of(env, buf, at);
    }
    free(text);
    free(c);
    free(buf);
    return out;
}

JNIEXPORT jbyteArray JNICALL
Java_works_squirrel_qr_SqQr_nativeWifi(JNIEnv *env, jclass cls, jbyteArray ssid, jbyteArray password,
                                       jbyteArray security, jboolean hidden) {
    (void)cls;
    char *s = cstr(env, ssid), *p = cstr(env, password), *t = cstr(env, security);
    jbyteArray out = NULL;
    if (s && p && t) {
        size_t n = sqqr_content_wifi(s, p, t, hidden ? 1 : 0, NULL, 0);
        char *buf = malloc(n + 1);
        if (buf) {
            sqqr_content_wifi(s, p, t, hidden ? 1 : 0, buf, n + 1);
            out = bytes_of(env, buf, n);
            free(buf);
        }
    }
    free(s);
    free(p);
    free(t);
    return out;
}

/*
 * JNI bridge: SquirrelSuicaAnalysis -> libs/sqfelica.
 *
 * History comes back as a flat int array, SQF_JNI_FIELDS ints per entry, in
 * the order documented in SqFelica.kt. Keep the two in step.
 */
#include <jni.h>
#include <stdint.h>
#include <string.h>

#include "sqfelica.h"

#define SQF_JNI_FIELDS 18
#define SQF_JNI_MAX 64

enum { BRAND_SUICA = 0, BRAND_NANACO = 1, BRAND_EDY = 2, BRAND_WAON = 3 };

static void flatten(const SqfHistory *h, jint *o) {
    o[0] = (jint)h->kind;
    o[1] = h->year;
    o[2] = h->month;
    o[3] = h->day;
    o[4] = h->hour;
    o[5] = h->minute;
    o[6] = h->fare_yen;
    o[7] = h->balance_yen;
    o[8] = h->amount_yen;
    o[9] = h->terminal;
    o[10] = h->process;
    o[11] = h->has_enter;
    o[12] = h->enter_line;
    o[13] = h->enter_station;
    o[14] = h->has_exit;
    o[15] = h->exit_line;
    o[16] = h->exit_station;
    o[17] = h->region;
}

JNIEXPORT jintArray JNICALL
Java_works_squirrel_suicaanalysis_SqFelica_nativeHistory(JNIEnv *env, jclass cls, jint brand,
                                                        jbyteArray blocks) {
    (void)cls;
    uint8_t buf[SQF_JNI_MAX * SQF_BLOCK_LEN];
    SqfHistory h[SQF_JNI_MAX];
    jint flat[SQF_JNI_MAX * SQF_JNI_FIELDS];
    size_t n = 0, k = 0;
    if (blocks) {
        jsize len = (*env)->GetArrayLength(env, blocks);
        n = (size_t)len / SQF_BLOCK_LEN;
        if (n > SQF_JNI_MAX) n = SQF_JNI_MAX;
        (*env)->GetByteArrayRegion(env, blocks, 0, (jsize)(n * SQF_BLOCK_LEN), (jbyte *)buf);
    }
    switch (brand) {
        case BRAND_SUICA: k = sqf_parse_suica_history(buf, n, h, SQF_JNI_MAX); break;
        case BRAND_NANACO: k = sqf_parse_nanaco_history(buf, n, h, SQF_JNI_MAX); break;
        case BRAND_EDY: k = sqf_parse_edy_history(buf, n, h, SQF_JNI_MAX); break;
        case BRAND_WAON: k = sqf_parse_waon_history(buf, n, h, SQF_JNI_MAX); break;
        default: k = 0; break;
    }
    for (size_t i = 0; i < k; i++) flatten(&h[i], flat + i * SQF_JNI_FIELDS);
    jintArray out = (*env)->NewIntArray(env, (jsize)(k * SQF_JNI_FIELDS));
    if (out && k) (*env)->SetIntArrayRegion(env, out, 0, (jsize)(k * SQF_JNI_FIELDS), flat);
    return out;
}

/* Balance in yen, or -1 when the blocks needed are missing. b is only used for Suica (0x008B). */
JNIEXPORT jint JNICALL
Java_works_squirrel_suicaanalysis_SqFelica_nativeBalance(JNIEnv *env, jclass cls, jint brand,
                                                        jbyteArray a, jbyteArray b) {
    (void)cls;
    uint8_t ab[SQF_BLOCK_LEN], bb[SQF_BLOCK_LEN];
    size_t al = 0, bl = 0;
    if (a) {
        al = (size_t)(*env)->GetArrayLength(env, a);
        if (al > SQF_BLOCK_LEN) al = SQF_BLOCK_LEN;
        (*env)->GetByteArrayRegion(env, a, 0, (jsize)al, (jbyte *)ab);
    }
    if (b) {
        bl = (size_t)(*env)->GetArrayLength(env, b);
        if (bl > SQF_BLOCK_LEN) bl = SQF_BLOCK_LEN;
        (*env)->GetByteArrayRegion(env, b, 0, (jsize)bl, (jbyte *)bb);
    }
    switch (brand) {
        case BRAND_SUICA: return sqf_suica_balance(a ? ab : NULL, al, b ? bb : NULL, bl);
        case BRAND_NANACO: return sqf_nanaco_balance(a ? ab : NULL, al);
        case BRAND_EDY: return sqf_edy_balance(a ? ab : NULL, al);
        case BRAND_WAON: return sqf_waon_balance(a ? ab : NULL, al);
        default: return -1;
    }
}

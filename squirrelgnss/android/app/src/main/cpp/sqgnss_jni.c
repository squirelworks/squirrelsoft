/*
 * JNI bridge: SquirrelGNSS Android -> squirrelgnss/src/sqgnss.c.
 *
 * nativeLlhToEcef(llh, ecef) / nativeEcefToLlh(ecef, llh) convert WGS84
 * geodetic (lat_deg, lon_deg, height_m) and ECEF metres both ways.
 *
 * nativeStakeoutDelta(fromLlh, toLlh, enu) is sqg_stakeout_delta: east,
 * north, up in metres from one site to another.
 *
 * nativeDemoSolve(lat, lon, h, tGps, clockBiasM, noiseM, seed, out) builds a
 * synthetic sky (sqg_demo_ephemerides + sqg_demo_epoch), solves PVT, and on
 * success writes out (length >= OUT_LEN):
 *   [0..2] solution lat_deg, lon_deg, height_m
 *   [3] clock_bias_m, [4] n_used, [5] hdop, [6] vdop, [7] rms_m,
 *   [8] 3-D error vs the truth site in metres.
 *
 * nativeFillDemoEpoch(...) writes packed observations for the synthetic sky
 * into parallel prn / range / snr arrays and the epoch's t_receiver
 * (t_true + clock_bias/c) into tReceiverOut[0] — CI and unit tests.
 *
 * nativeSolveEpoch(tReceiver, tEph, prns, ranges, snrs, n, truthLlhOrNull, out)
 * solves PVT from a caller-supplied observation epoch against demo
 * ephemerides at tEph — the path phone GnssMeasurement / Location feeds use
 * after packing. For a fillDemoEpoch round-trip, pass that call's t_true as
 * tEph and the written t_receiver as tReceiver.
 *
 * SqGnss.kt / the host jni-smoke read them in this order; keep them in step.
 */
#include <jni.h>
#include <string.h>

#include "sqgnss.h"

#define OUT_LEN 9

static int need3(JNIEnv *env, jdoubleArray a) {
    return a && (*env)->GetArrayLength(env, a) >= 3;
}

static int write_pvt(JNIEnv *env, const SqgPvt *pvt, const SqgEcef *truth_or_null, jdoubleArray out) {
    if (!out || (*env)->GetArrayLength(env, out) < OUT_LEN) return 0;
    if (!pvt->converged) return 0;
    SqgGeodetic sol;
    sqg_ecef_to_geodetic(pvt->pos, &sol);
    jdouble o[OUT_LEN];
    o[0] = sol.lat_deg;
    o[1] = sol.lon_deg;
    o[2] = sol.height_m;
    o[3] = pvt->clock_bias_m;
    o[4] = (double)pvt->n_used;
    o[5] = pvt->hdop;
    o[6] = pvt->vdop;
    o[7] = pvt->rms_m;
    o[8] = truth_or_null ? sqg_baseline_length(pvt->pos, *truth_or_null) : -1.0;
    (*env)->SetDoubleArrayRegion(env, out, 0, OUT_LEN, o);
    return 1;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeLlhToEcef(JNIEnv *env, jclass cls, jdoubleArray llh, jdoubleArray ecef) {
    (void)cls;
    if (!need3(env, llh) || !need3(env, ecef)) return JNI_FALSE;
    jdouble g[3], xyz[3];
    (*env)->GetDoubleArrayRegion(env, llh, 0, 3, g);
    SqgEcef out;
    sqg_geodetic_to_ecef((SqgGeodetic){g[0], g[1], g[2]}, &out);
    xyz[0] = out.x;
    xyz[1] = out.y;
    xyz[2] = out.z;
    (*env)->SetDoubleArrayRegion(env, ecef, 0, 3, xyz);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeEcefToLlh(JNIEnv *env, jclass cls, jdoubleArray ecef, jdoubleArray llh) {
    (void)cls;
    if (!need3(env, ecef) || !need3(env, llh)) return JNI_FALSE;
    jdouble xyz[3], g[3];
    (*env)->GetDoubleArrayRegion(env, ecef, 0, 3, xyz);
    SqgGeodetic out;
    sqg_ecef_to_geodetic((SqgEcef){xyz[0], xyz[1], xyz[2]}, &out);
    g[0] = out.lat_deg;
    g[1] = out.lon_deg;
    g[2] = out.height_m;
    (*env)->SetDoubleArrayRegion(env, llh, 0, 3, g);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeStakeoutDelta(JNIEnv *env, jclass cls, jdoubleArray from_llh,
                                                    jdoubleArray to_llh, jdoubleArray enu) {
    (void)cls;
    if (!need3(env, from_llh) || !need3(env, to_llh) || !need3(env, enu)) return JNI_FALSE;
    jdouble a[3], b[3], d[3];
    (*env)->GetDoubleArrayRegion(env, from_llh, 0, 3, a);
    (*env)->GetDoubleArrayRegion(env, to_llh, 0, 3, b);
    SqgEnu delta = sqg_stakeout_delta((SqgGeodetic){a[0], a[1], a[2]}, (SqgGeodetic){b[0], b[1], b[2]});
    d[0] = delta.e;
    d[1] = delta.n;
    d[2] = delta.u;
    (*env)->SetDoubleArrayRegion(env, enu, 0, 3, d);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeDemoSolve(JNIEnv *env, jclass cls, jdouble lat, jdouble lon, jdouble height_m,
                                                jdouble t_gps, jdouble clock_bias_m, jdouble noise_m, jint seed,
                                                jdoubleArray out) {
    (void)cls;
    SqgEphemeris eph[SQG_DEMO_SATS];
    int n_eph = 0;
    sqg_demo_ephemerides(eph, &n_eph, t_gps);
    SqgGeodetic truth = {lat, lon, height_m};
    SqgEcef truth_ecef;
    sqg_geodetic_to_ecef(truth, &truth_ecef);
    SqgEpoch epoch;
    sqg_demo_epoch(eph, n_eph, truth, t_gps, clock_bias_m, 5.0, noise_m, (unsigned)seed, &epoch);
    SqgPvt pvt;
    if (sqg_solve_pvt(&epoch, eph, n_eph, (SqgEcef){0, 0, 0}, &pvt) != 0) return JNI_FALSE;
    return write_pvt(env, &pvt, &truth_ecef, out) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_works_squirrel_gnss_SqGnss_nativeFillDemoEpoch(JNIEnv *env, jclass cls, jdouble lat, jdouble lon, jdouble height_m,
                                                    jdouble t_gps, jdouble clock_bias_m, jdouble noise_m, jint seed,
                                                    jdoubleArray t_receiver_out, jintArray prns, jdoubleArray ranges,
                                                    jdoubleArray snrs) {
    (void)cls;
    if (!prns || !ranges || !snrs || !t_receiver_out) return -1;
    if ((*env)->GetArrayLength(env, t_receiver_out) < 1) return -1;
    jsize cap = (*env)->GetArrayLength(env, prns);
    if ((*env)->GetArrayLength(env, ranges) < cap || (*env)->GetArrayLength(env, snrs) < cap) return -1;
    SqgEphemeris eph[SQG_DEMO_SATS];
    int n_eph = 0;
    sqg_demo_ephemerides(eph, &n_eph, t_gps);
    SqgEpoch epoch;
    sqg_demo_epoch(eph, n_eph, (SqgGeodetic){lat, lon, height_m}, t_gps, clock_bias_m, 5.0, noise_m, (unsigned)seed,
                   &epoch);
    /* t_receiver is t_true + clock_bias/c — solveEpoch must use this, not t_true. */
    jdouble tr = epoch.t_receiver;
    (*env)->SetDoubleArrayRegion(env, t_receiver_out, 0, 1, &tr);
    int n = epoch.n_obs;
    if (n > cap) n = (int)cap;
    if (n <= 0) return 0;
    jint *p = (*env)->GetIntArrayElements(env, prns, NULL);
    jdouble *r = (*env)->GetDoubleArrayElements(env, ranges, NULL);
    jdouble *s = (*env)->GetDoubleArrayElements(env, snrs, NULL);
    if (!p || !r || !s) {
        if (p) (*env)->ReleaseIntArrayElements(env, prns, p, JNI_ABORT);
        if (r) (*env)->ReleaseDoubleArrayElements(env, ranges, r, JNI_ABORT);
        if (s) (*env)->ReleaseDoubleArrayElements(env, snrs, s, JNI_ABORT);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        p[i] = epoch.obs[i].prn;
        r[i] = epoch.obs[i].pseudorange_m;
        s[i] = epoch.obs[i].snr_dbhz;
    }
    (*env)->ReleaseIntArrayElements(env, prns, p, 0);
    (*env)->ReleaseDoubleArrayElements(env, ranges, r, 0);
    (*env)->ReleaseDoubleArrayElements(env, snrs, s, 0);
    return n;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeSolveEpoch(JNIEnv *env, jclass cls, jdouble t_receiver, jdouble t_eph,
                                                 jintArray prns, jdoubleArray ranges, jdoubleArray snrs, jint n,
                                                 jdoubleArray truth_llh, jdoubleArray out) {
    (void)cls;
    if (!prns || !ranges || !snrs || n < 4 || n > SQG_MAX_SATS) return JNI_FALSE;
    if ((*env)->GetArrayLength(env, prns) < n || (*env)->GetArrayLength(env, ranges) < n ||
        (*env)->GetArrayLength(env, snrs) < n)
        return JNI_FALSE;

    SqgEpoch epoch;
    memset(&epoch, 0, sizeof epoch);
    epoch.t_receiver = t_receiver;
    epoch.n_obs = n;
    jint *p = (*env)->GetIntArrayElements(env, prns, NULL);
    jdouble *r = (*env)->GetDoubleArrayElements(env, ranges, NULL);
    jdouble *s = (*env)->GetDoubleArrayElements(env, snrs, NULL);
    if (!p || !r || !s) {
        if (p) (*env)->ReleaseIntArrayElements(env, prns, p, JNI_ABORT);
        if (r) (*env)->ReleaseDoubleArrayElements(env, ranges, r, JNI_ABORT);
        if (s) (*env)->ReleaseDoubleArrayElements(env, snrs, s, JNI_ABORT);
        return JNI_FALSE;
    }
    for (int i = 0; i < n; i++) {
        epoch.obs[i].prn = p[i];
        epoch.obs[i].pseudorange_m = r[i];
        epoch.obs[i].snr_dbhz = s[i];
        epoch.obs[i].valid = 1;
    }
    (*env)->ReleaseIntArrayElements(env, prns, p, JNI_ABORT);
    (*env)->ReleaseDoubleArrayElements(env, ranges, r, JNI_ABORT);
    (*env)->ReleaseDoubleArrayElements(env, snrs, s, JNI_ABORT);

    SqgEphemeris eph[SQG_DEMO_SATS];
    int n_eph = 0;
    /* Match nativeDemoSolve: demo ephemerides at t_true (t_eph), not t_receiver. */
    sqg_demo_ephemerides(eph, &n_eph, t_eph);
    SqgPvt pvt;
    if (sqg_solve_pvt(&epoch, eph, n_eph, (SqgEcef){0, 0, 0}, &pvt) != 0) return JNI_FALSE;

    SqgEcef truth_ecef, *truth_ptr = NULL;
    if (truth_llh && (*env)->GetArrayLength(env, truth_llh) >= 3) {
        jdouble g[3];
        (*env)->GetDoubleArrayRegion(env, truth_llh, 0, 3, g);
        sqg_geodetic_to_ecef((SqgGeodetic){g[0], g[1], g[2]}, &truth_ecef);
        truth_ptr = &truth_ecef;
    }
    return write_pvt(env, &pvt, truth_ptr, out) ? JNI_TRUE : JNI_FALSE;
}

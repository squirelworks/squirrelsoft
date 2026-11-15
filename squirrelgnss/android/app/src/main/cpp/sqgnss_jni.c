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
 * SqGnss.kt / the host jni-smoke read them in this order; keep them in step.
 * No phone GNSS hardware is used — CI and the smoke test stay synthetic.
 */
#include <jni.h>
#include <string.h>

#include "sqgnss.h"

#define OUT_LEN 9

static int need3(JNIEnv *env, jdoubleArray a) {
    return a && (*env)->GetArrayLength(env, a) >= 3;
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
    if (!out || (*env)->GetArrayLength(env, out) < OUT_LEN) return JNI_FALSE;
    SqgEphemeris eph[SQG_DEMO_SATS];
    int n_eph = 0;
    sqg_demo_ephemerides(eph, &n_eph, t_gps);
    SqgGeodetic truth = {lat, lon, height_m};
    SqgEcef truth_ecef;
    sqg_geodetic_to_ecef(truth, &truth_ecef);
    SqgEpoch epoch;
    sqg_demo_epoch(eph, n_eph, truth, t_gps, clock_bias_m, 5.0, noise_m, (unsigned)seed, &epoch);
    SqgPvt pvt;
    if (sqg_solve_pvt(&epoch, eph, n_eph, (SqgEcef){0, 0, 0}, &pvt) != 0 || !pvt.converged) return JNI_FALSE;
    SqgGeodetic sol;
    sqg_ecef_to_geodetic(pvt.pos, &sol);
    jdouble o[OUT_LEN];
    o[0] = sol.lat_deg;
    o[1] = sol.lon_deg;
    o[2] = sol.height_m;
    o[3] = pvt.clock_bias_m;
    o[4] = (double)pvt.n_used;
    o[5] = pvt.hdop;
    o[6] = pvt.vdop;
    o[7] = pvt.rms_m;
    o[8] = sqg_baseline_length(pvt.pos, truth_ecef);
    (*env)->SetDoubleArrayRegion(env, out, 0, OUT_LEN, o);
    return JNI_TRUE;
}

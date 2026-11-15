/*
 * JNI bridge: SquirrelGNSS Android -> squirrelgnss portable cores.
 *
 * Positioning (sqgnss.c):
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
 * NTRIP + RTCM (sqntrip + sqrtcm):
 * nativeNtripSynthMsm(out) builds a synthetic MSM4 frame, wraps it in an
 * ICY 200 reply, runs SqntClient over memio, then sqrtcm_framer_feed +
 * sqrtcm_parse_msm. CI / host smoke only — no network.
 *
 * nativeNtripFeedResponse(bytes, out) feeds a full caster reply (headers +
 * body) through the same parser → framer → MSM path.
 *
 * nativeNtripDial(host, port, mount, user, pass, ntripVersion, out) opens
 * a live TCP session (operator path; never dialled from CI). Empty user/pass
 * skips Basic auth. out layout is NTRIP_OUT_LEN ints (see write_ntrip_msm).
 *
 * SqGnss.kt / the host jni-smoke read them in this order; keep them in step.
 */
#include <jni.h>
#include <stdio.h>
#include <string.h>

#include "sqgnss.h"
#include "sqntrip.h"
#include "sqrtcm.h"

#define OUT_LEN 9
#define NTRIP_OUT_LEN 12
#define NTRIP_BODY_CAP 4096

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

/* ---- NTRIP / RTCM (synthetic MSM4; same constants as tests/test_rtcm.c) ---- */

static void fill_synth_msm4(SqrtcmMsm *m) {
    memset(m, 0, sizeof *m);
    m->msg_type = 1074;
    m->msm_type = 4;
    m->station_id = 2001;
    m->epoch_ms = 345600000u;
    m->multi = 0;
    m->n_sat = 2;
    m->sat_id[0] = 3;
    m->sat_id[1] = 7;
    m->n_sig = 2;
    m->sig_id[0] = 2;
    m->sig_id[1] = 16;
    m->n_cell = 3;
    m->cell_sat[0] = 0;
    m->cell_sig[0] = 0;
    m->cell_sat[1] = 0;
    m->cell_sig[1] = 1;
    m->cell_sat[2] = 1;
    m->cell_sig[2] = 0;
    m->rough_ms[0] = 70;
    m->rough_mod[0] = 512;
    m->rough_ms[1] = 71;
    m->rough_mod[1] = 0;
    m->fine_pr[0] = 100;
    m->fine_phase[0] = 200;
    m->lock[0] = 5;
    m->half_cycle[0] = 0;
    m->cnr[0] = 40;
    m->fine_pr[1] = -50;
    m->fine_phase[1] = -100;
    m->lock[1] = 3;
    m->half_cycle[1] = 1;
    m->cnr[1] = 35;
    m->fine_pr[2] = 0;
    m->fine_phase[2] = 1;
    m->lock[2] = 7;
    m->half_cycle[2] = 0;
    m->cnr[2] = 42;
}

static int build_synth_msm4_frame(unsigned char *frame, size_t cap, size_t *out_len) {
    SqrtcmMsm msm;
    fill_synth_msm4(&msm);
    unsigned char payload[256];
    size_t plen = sqrtcm_pack_msm(payload, sizeof payload, &msm);
    if (plen == 0) return -1;
    size_t flen = sqrtcm_frame(frame, cap, payload, plen);
    if (flen == 0) return -1;
    *out_len = flen;
    return 0;
}

/* out[0..11]: status, kind, body_len, n_frames, msg_type, station_id,
 * epoch_ms, msm_type, n_sat, n_sig, n_cell, category */
static int write_ntrip_msm(JNIEnv *env, jintArray out, const SqntParser *p, int n_frames,
                           const SqrtcmMsg *msg, const SqrtcmMsm *msm) {
    if (!out || (*env)->GetArrayLength(env, out) < NTRIP_OUT_LEN) return 0;
    jint o[NTRIP_OUT_LEN];
    o[0] = p ? p->status : 0;
    o[1] = p ? p->kind : 0;
    o[2] = p ? (jint)p->body_len : 0;
    o[3] = n_frames;
    o[4] = msg ? (jint)msg->msg_type : 0;
    o[5] = msg ? (jint)msg->station_id : 0;
    o[6] = msg ? (jint)msg->epoch_ms : 0;
    o[7] = msm ? msm->msm_type : 0;
    o[8] = msm ? msm->n_sat : 0;
    o[9] = msm ? msm->n_sig : 0;
    o[10] = msm ? msm->n_cell : 0;
    o[11] = msg ? msg->category : 0;
    (*env)->SetIntArrayRegion(env, out, 0, NTRIP_OUT_LEN, o);
    return 1;
}

static int ntrip_body_to_msm(const unsigned char *body, size_t body_len, SqrtcmMsg *msg,
                             SqrtcmMsm *msm, int *n_frames) {
    SqrtcmFramer f;
    sqrtcm_framer_init(&f);
    *n_frames = sqrtcm_framer_feed(&f, body, body_len, msg, 1);
    if (*n_frames < 1) return -1;
    if (sqrtcm_parse_msm(sqrtcm_msg_payload(msg), msg->payload_len, msm) != 0) return -1;
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeNtripSynthMsm(JNIEnv *env, jclass cls, jintArray out) {
    (void)cls;
    unsigned char frame[512];
    size_t flen = 0;
    if (build_synth_msm4_frame(frame, sizeof frame, &flen) != 0) return JNI_FALSE;

    unsigned char reply[640];
    const char *hdr = "ICY 200 OK\r\nServer: SquirrelGNSS-Synth\r\n\r\n";
    size_t hlen = strlen(hdr);
    if (hlen + flen > sizeof reply) return JNI_FALSE;
    memcpy(reply, hdr, hlen);
    memcpy(reply + hlen, frame, flen);

    unsigned char body[NTRIP_BODY_CAP];
    unsigned char tx[512];
    SqntMemIo mem;
    sqnt_memio_init(&mem, reply, hlen + flen, tx, sizeof tx);
    SqntClient cli;
    sqnt_client_init(&cli, sqnt_memio_io(&mem), body, sizeof body);
    SqntRequest req;
    sqnt_request_init(&req);
    snprintf(req.mount, sizeof req.mount, "SYNTH");
    if (sqnt_client_request(&cli, &req) != 0) {
        sqnt_client_close(&cli);
        return JNI_FALSE;
    }
    if (sqnt_client_drain_body(&cli) != 0) {
        sqnt_client_close(&cli);
        return JNI_FALSE;
    }
    if (!cli.parser.header_done || cli.parser.status != 200 ||
        cli.parser.kind != SQNT_KIND_STREAM) {
        sqnt_client_close(&cli);
        return JNI_FALSE;
    }

    SqrtcmMsg msg;
    SqrtcmMsm msm;
    int n_frames = 0;
    int rc = ntrip_body_to_msm(body, cli.parser.body_len, &msg, &msm, &n_frames);
    int ok = (rc == 0) && write_ntrip_msm(env, out, &cli.parser, n_frames, &msg, &msm);
    sqnt_client_close(&cli);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeNtripFeedResponse(JNIEnv *env, jclass cls, jbyteArray bytes,
                                                        jintArray out) {
    (void)cls;
    if (!bytes) return JNI_FALSE;
    jsize n = (*env)->GetArrayLength(env, bytes);
    if (n <= 0) return JNI_FALSE;
    jbyte *raw = (*env)->GetByteArrayElements(env, bytes, NULL);
    if (!raw) return JNI_FALSE;

    unsigned char body[NTRIP_BODY_CAP];
    SqntParser p;
    sqnt_parser_init(&p, body, sizeof body);
    int feed_rc = sqnt_parser_feed(&p, raw, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, bytes, raw, JNI_ABORT);
    if (feed_rc != 0 || !p.header_done || p.status != 200 || p.kind != SQNT_KIND_STREAM) {
        return JNI_FALSE;
    }

    SqrtcmMsg msg;
    SqrtcmMsm msm;
    int n_frames = 0;
    if (ntrip_body_to_msm(body, p.body_len, &msg, &msm, &n_frames) != 0) return JNI_FALSE;
    return write_ntrip_msm(env, out, &p, n_frames, &msg, &msm) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeNtripDial(JNIEnv *env, jclass cls, jstring host, jstring port,
                                                jstring mount, jstring user, jstring pass,
                                                jint ntrip_version, jintArray out) {
    (void)cls;
    if (!host || !port || !mount) return JNI_FALSE;

    const char *c_host = (*env)->GetStringUTFChars(env, host, NULL);
    const char *c_port = (*env)->GetStringUTFChars(env, port, NULL);
    const char *c_mount = (*env)->GetStringUTFChars(env, mount, NULL);
    const char *c_user = user ? (*env)->GetStringUTFChars(env, user, NULL) : NULL;
    const char *c_pass = pass ? (*env)->GetStringUTFChars(env, pass, NULL) : NULL;
    if (!c_host || !c_port || !c_mount) {
        if (c_host) (*env)->ReleaseStringUTFChars(env, host, c_host);
        if (c_port) (*env)->ReleaseStringUTFChars(env, port, c_port);
        if (c_mount) (*env)->ReleaseStringUTFChars(env, mount, c_mount);
        if (c_user) (*env)->ReleaseStringUTFChars(env, user, c_user);
        if (c_pass) (*env)->ReleaseStringUTFChars(env, pass, c_pass);
        return JNI_FALSE;
    }

    SqntTcp tcp;
    char err[192];
    unsigned char body[NTRIP_BODY_CAP];
    SqntClient cli;
    jboolean ok = JNI_FALSE;

    if (sqnt_tcp_connect(&tcp, c_host, c_port, 0, err, sizeof err) != 0) {
        goto done;
    }
    sqnt_client_init(&cli, sqnt_tcp_io(&tcp), body, sizeof body);
    SqntRequest req;
    sqnt_request_init(&req);
    snprintf(req.host, sizeof req.host, "%s", c_host);
    snprintf(req.mount, sizeof req.mount, "%s", c_mount);
    if (c_user && c_user[0]) snprintf(req.user, sizeof req.user, "%s", c_user);
    if (c_pass && c_pass[0]) snprintf(req.password, sizeof req.password, "%s", c_pass);
    req.ntrip_version = (ntrip_version == 2) ? 2 : 1;

    if (sqnt_client_request(&cli, &req) != 0) {
        sqnt_client_close(&cli);
        goto done;
    }
    /* Drain until body has some RTCM or EOF / capacity. Cap pumps so a silent
     * caster cannot hang the UI thread forever. */
    for (int i = 0; i < 64; i++) {
        if (cli.parser.header_done && cli.parser.body_len > 0) break;
        int pr = sqnt_client_pump(&cli);
        if (pr != 0) break;
    }
    if (!cli.parser.header_done || cli.parser.status != 200 ||
        cli.parser.kind != SQNT_KIND_STREAM) {
        sqnt_client_close(&cli);
        goto done;
    }
    /* A few more pumps for a short correction burst, then stop. */
    for (int i = 0; i < 32 && cli.parser.body_len < sizeof body - 64; i++) {
        int pr = sqnt_client_pump(&cli);
        if (pr != 0) break;
    }

    SqrtcmMsg msg;
    SqrtcmMsm msm;
    int n_frames = 0;
    if (ntrip_body_to_msm(body, cli.parser.body_len, &msg, &msm, &n_frames) == 0 &&
        write_ntrip_msm(env, out, &cli.parser, n_frames, &msg, &msm)) {
        ok = JNI_TRUE;
    }
    sqnt_client_close(&cli);

done:
    (*env)->ReleaseStringUTFChars(env, host, c_host);
    (*env)->ReleaseStringUTFChars(env, port, c_port);
    (*env)->ReleaseStringUTFChars(env, mount, c_mount);
    if (c_user) (*env)->ReleaseStringUTFChars(env, user, c_user);
    if (c_pass) (*env)->ReleaseStringUTFChars(env, pass, c_pass);
    return ok;
}

JNIEXPORT jboolean JNICALL
Java_works_squirrel_gnss_SqGnss_nativeNtripLoopbackMsm(JNIEnv *env, jclass cls, jintArray out) {
    (void)cls;
    /* 127.0.0.1 synthetic caster: proves Android SqntTcp + framer → MSM. */
    SqntTcp listener, server, client;
    char err[160];
    int port = 0;
    if (sqnt_tcp_listen_loopback(&listener, &port, err, sizeof err) != 0) return JNI_FALSE;

    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    if (sqnt_tcp_connect(&client, "127.0.0.1", portstr, 0, err, sizeof err) != 0 ||
        sqnt_tcp_accept(&listener, &server, err, sizeof err) != 0) {
        sqnt_tcp_close(&listener);
        return JNI_FALSE;
    }

    unsigned char body[NTRIP_BODY_CAP];
    SqntClient cli;
    sqnt_client_init(&cli, sqnt_tcp_io(&client), body, sizeof body);
    SqntRequest req;
    sqnt_request_init(&req);
    snprintf(req.mount, sizeof req.mount, "SYNTH");
    if (sqnt_client_request(&cli, &req) != 0) {
        sqnt_client_close(&cli);
        sqnt_tcp_close(&server);
        sqnt_tcp_close(&listener);
        return JNI_FALSE;
    }

    /* Read GET on the server side, then reply with ICY + MSM4 frame. */
    SqntIo rio = sqnt_tcp_io(&server);
    rio.close = NULL;
    char got[512];
    size_t got_len = 0;
    got[0] = 0;
    unsigned char chunk[128];
    while (!strstr(got, "\r\n\r\n") && got_len < sizeof got - 1) {
        long rn = rio.read(rio.ctx, chunk, sizeof chunk);
        if (rn <= 0) break;
        if (got_len + (size_t)rn >= sizeof got) break;
        memcpy(got + got_len, chunk, (size_t)rn);
        got_len += (size_t)rn;
        got[got_len] = 0;
    }

    unsigned char frame[512];
    size_t flen = 0;
    jboolean ok = JNI_FALSE;
    if (build_synth_msm4_frame(frame, sizeof frame, &flen) == 0) {
        unsigned char reply[640];
        const char *hdr = "ICY 200 OK\r\n\r\n";
        size_t hlen = strlen(hdr);
        memcpy(reply, hdr, hlen);
        memcpy(reply + hlen, frame, flen);
        rio.write(rio.ctx, reply, hlen + flen);
        sqnt_tcp_close(&server);

        if (sqnt_client_drain_body(&cli) == 0 && cli.parser.header_done &&
            cli.parser.status == 200 && cli.parser.kind == SQNT_KIND_STREAM) {
            SqrtcmMsg msg;
            SqrtcmMsm msm;
            int n_frames = 0;
            if (ntrip_body_to_msm(body, cli.parser.body_len, &msg, &msm, &n_frames) == 0 &&
                write_ntrip_msm(env, out, &cli.parser, n_frames, &msg, &msm)) {
                ok = JNI_TRUE;
            }
        }
    } else {
        sqnt_tcp_close(&server);
    }

    sqnt_client_close(&cli);
    sqnt_tcp_close(&listener);
    return ok;
}

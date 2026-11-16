#define _POSIX_C_SOURCE 200809L
#include "fl_hil.h"

#include "squirrelnum.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *fl_hil_frame_kind_name(FlHilFrameKind k) {
    switch (k) {
    case FL_HIL_FR_HELLO:
        return "HELLO";
    case FL_HIL_FR_HELLO_ACK:
        return "HELLO_ACK";
    case FL_HIL_FR_SYNC:
        return "SYNC";
    case FL_HIL_FR_CH:
        return "CH";
    case FL_HIL_FR_BYE:
        return "BYE";
    case FL_HIL_FR_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

int fl_hil_encode_hello(char *out, int out_cap) {
    if (!out || out_cap < 8) return -1;
    return snprintf(out, out_cap, "HELLO SoftFlightLab/1.0");
}

int fl_hil_encode_hello_ack(char *out, int out_cap, const char *board_id, int nchannels) {
    if (!out || out_cap < 16 || !board_id) return -1;
    return snprintf(out, out_cap, "HELLO_ACK %s channels=%d protocol=%s", board_id,
                    nchannels, FL_HIL_PROTO);
}

int fl_hil_encode_sync(char *out, int out_cap, double t_s) {
    if (!out || out_cap < 8) return -1;
    return snprintf(out, out_cap, "SYNC t_s=%.4f", t_s);
}

int fl_hil_encode_ch(char *out, int out_cap, const char *role, int pin, double value) {
    if (!out || out_cap < 16 || !role) return -1;
    return snprintf(out, out_cap, "CH role=%s pin=%d value=%.6f", role, pin, value);
}

int fl_hil_encode_bye(char *out, int out_cap) {
    if (!out || out_cap < 4) return -1;
    return snprintf(out, out_cap, "BYE");
}

static int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

int fl_hil_parse_line(const char *line, FlHilFrame *fr) {
    if (!line || !fr) return -1;
    memset(fr, 0, sizeof *fr);
    while (*line == ' ' || *line == '\t') line++;
    if (!*line || *line == '#') {
        fr->kind = FL_HIL_FR_UNKNOWN;
        return 0;
    }
    snprintf(fr->line, sizeof fr->line, "%s", line);
    if (starts_with(line, "HELLO_ACK")) {
        fr->kind = FL_HIL_FR_HELLO_ACK;
        /* HELLO_ACK board channels=N protocol=SQHIL1 */
        char board[64];
        int nch = 0;
        char proto[16];
        if (sscanf(line, "HELLO_ACK %63s channels=%d protocol=%15s", board, &nch, proto) >=
            2) {
            snprintf(fr->role, sizeof fr->role, "%s", board);
            fr->channels = nch;
            if (proto[0])
                snprintf(fr->line, sizeof fr->line, "%s", proto); /* reuse: proto tag */
        }
        return 0;
    }
    if (starts_with(line, "HELLO")) {
        fr->kind = FL_HIL_FR_HELLO;
        return 0;
    }
    if (starts_with(line, "SYNC")) {
        fr->kind = FL_HIL_FR_SYNC;
        if (sscanf(line, "SYNC t_s=%lf", &fr->t_s) != 1) return -1;
        return 0;
    }
    if (starts_with(line, "CH ")) {
        fr->kind = FL_HIL_FR_CH;
        if (sscanf(line, "CH role=%31s pin=%d value=%lf", fr->role, &fr->pin, &fr->value) !=
            3)
            return -1;
        return 0;
    }
    if (starts_with(line, "BYE")) {
        fr->kind = FL_HIL_FR_BYE;
        return 0;
    }
    if (starts_with(line, "ERROR")) {
        fr->kind = FL_HIL_FR_ERROR;
        return 0;
    }
    fr->kind = FL_HIL_FR_UNKNOWN;
    return -1;
}

int fl_hil_session_from_text(FlHilSession *sess, const char *text, char *err, int err_cap) {
    if (!sess || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(sess, 0, sizeof *sess);
    snprintf(sess->protocol, sizeof sess->protocol, "%s", FL_HIL_PROTO);
    snprintf(sess->board_id, sizeof sess->board_id, "ISM-FJ-MOCK");

    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int saw_hdr = 0, saw_ack = 0, saw_bye = 0;
    double cur_t = 0.0;
    int line_no = 0;
    while (line) {
        line_no++;
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        size_t L = strlen(t);
        while (L > 0 && (t[L - 1] == '\r' || t[L - 1] == ' ')) t[--L] = 0;
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw_hdr) {
            if (!starts_with(t, "SQHIL1")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need SQHIL1 Soft mock header", line_no);
                free(buf);
                return -1;
            }
            saw_hdr = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        FlHilFrame fr;
        if (fl_hil_parse_line(t, &fr) != 0) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: bad frame", line_no);
            free(buf);
            return -1;
        }
        if (fr.kind == FL_HIL_FR_UNKNOWN) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (fr.kind == FL_HIL_FR_HELLO) {
            /* host hello recorded in transcript; ignore */
        } else if (fr.kind == FL_HIL_FR_HELLO_ACK) {
            saw_ack = 1;
            if (fr.role[0]) snprintf(sess->board_id, sizeof sess->board_id, "%s", fr.role);
            sess->nchannels_ack = fr.channels;
        } else if (fr.kind == FL_HIL_FR_SYNC) {
            cur_t = fr.t_s;
        } else if (fr.kind == FL_HIL_FR_CH) {
            if (sess->nsamples >= FL_HIL_MAX_SAMPLES) {
                if (err && err_cap) snprintf(err, err_cap, "too many samples");
                free(buf);
                return -1;
            }
            FlHilSample *s = &sess->samples[sess->nsamples++];
            snprintf(s->role, sizeof s->role, "%s", fr.role);
            s->pin = fr.pin;
            s->t_s = cur_t;
            s->value = fr.value;
        } else if (fr.kind == FL_HIL_FR_BYE) {
            saw_bye = 1;
        } else if (fr.kind == FL_HIL_FR_ERROR) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: ERROR frame", line_no);
            free(buf);
            return -1;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw_hdr || !saw_ack || !saw_bye || sess->nsamples < 2) {
        if (err && err_cap)
            snprintf(err, err_cap, "incomplete SQHIL1 session (ack=%d bye=%d n=%d)", saw_ack,
                     saw_bye, sess->nsamples);
        return -1;
    }
    sess->connected = 1;
    snprintf(sess->detail, sizeof sess->detail, "fixture session board=%s samples=%d",
             sess->board_id, sess->nsamples);
    return 0;
}

int fl_hil_session_from_sim(FlHilSession *sess, const FlSim *sim, char *err, int err_cap) {
    if (!sess || !sim || sim->nhist < 2) {
        if (err && err_cap) snprintf(err, err_cap, "need sim with >=2 history samples");
        return -1;
    }
    memset(sess, 0, sizeof *sess);
    snprintf(sess->protocol, sizeof sess->protocol, "%s", FL_HIL_PROTO);
    snprintf(sess->board_id, sizeof sess->board_id, "ISM-FJ-MOCK");
    sess->nchannels_ack = 14;

    FlDeviceExpect expect[FL_MAX_CHANNELS];
    int ne = fl_avionics_default_expect(expect, FL_MAX_CHANNELS);
    if (ne < 1) {
        if (err && err_cap) snprintf(err, err_cap, "no expect map");
        return -1;
    }

    /* Snapshot sim and walk history: re-init + step to each hist time is
     * heavy; instead sample kinematics from hist for rates and use controls. */
    int i;
    for (i = 0; i < sim->nhist; i++) {
        const FlHistSample *h = &sim->hist[i];
        /* Soft mock board mirrors host airdata / rates with zero sensor bias. */
        struct {
            const char *role;
            int pin;
            double value;
        } row[] = {
            {"imu_ax", 1, 0.0},
            {"imu_ay", 2, 0.0},
            {"imu_az", 3, FL_G0},
            {"imu_gx", 4, 0.0},
            {"imu_gy", 5, 0.0},
            {"imu_gz", 6, 0.0},
            {"radio_throttle", 7, sim->ctrl.throttle},
            {"radio_aileron", 8, sim->ctrl.da_deg},
            {"radio_elevator", 9, sim->ctrl.de_deg},
            {"radio_rudder", 10, sim->ctrl.dr_deg},
            {"act_aileron", 11, sim->ctrl.da_deg},
            {"act_elevator", 12, sim->ctrl.de_deg},
            {"act_rudder", 13, sim->ctrl.dr_deg},
            {"act_throttle", 14, sim->ctrl.throttle},
        };
        /* Encode nz into imu_az as Soft mock linkage for correlate role. */
        row[2].value = FL_G0 * (h->nz != 0.0 ? h->nz : 1.0);
        (void)ne;
        size_t r;
        for (r = 0; r < sizeof row / sizeof row[0]; r++) {
            if (sess->nsamples >= FL_HIL_MAX_SAMPLES) break;
            FlHilSample *s = &sess->samples[sess->nsamples++];
            snprintf(s->role, sizeof s->role, "%s", row[r].role);
            s->pin = row[r].pin;
            s->t_s = h->t;
            s->value = row[r].value;
        }
        if (sess->nsamples >= FL_HIL_MAX_SAMPLES) break;
    }
    if (sess->nsamples < 2) {
        if (err && err_cap) snprintf(err, err_cap, "mock session empty");
        return -1;
    }
    sess->connected = 1;
    snprintf(sess->detail, sizeof sess->detail,
             "Soft mock PCB session board=%s hist=%d samples=%d protocol=%s", sess->board_id,
             sim->nhist, sess->nsamples, sess->protocol);
    return 0;
}

static int looks_like_serial(const char *ep) {
    if (!ep) return 0;
    if (starts_with(ep, "/dev/")) return 1;
    if (starts_with(ep, "COM") || starts_with(ep, "com")) return 1;
    if (starts_with(ep, "serial:") || starts_with(ep, "usb:")) return 1;
    return 0;
}

static int read_whole_file(const char *path, char **out, char *err, int err_cap) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && err_cap) snprintf(err, err_cap, "cannot open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "seek %s", path);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "bad size %s", path);
        return -1;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "read %s", path);
        return -1;
    }
    buf[n] = 0;
    fclose(f);
    *out = buf;
    return 0;
}

int fl_hil_connect(const char *endpoint, FlHilSession *sess, FlSim *host_sim_out,
                   char *err, int err_cap) {
    if (!endpoint || !sess) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    if (looks_like_serial(endpoint)) {
        if (err && err_cap)
            snprintf(err, err_cap,
                     "live HIL serial/USB (%s) deferred until Victor freezes avionics "
                     "PCB ICD; use Soft mock: `hil mock` or a fixtures/hil/*.sqhil file "
                     "(S7.5 Soft path; OS/2 host gaps: squirrel-os2#225)",
                     endpoint);
        return -1;
    }

    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlSim sim;
    fl_sim_init(&sim, &p);
    FlControls c = {0.55, -1.0, 0.0, 0.0};
    fl_sim_set_controls(&sim, &c);
    if (fl_sim_run(&sim, 0.5, 0.05, 0.05) < 2) {
        if (err && err_cap) snprintf(err, err_cap, "host sim failed");
        return -1;
    }

    int rc;
    if (strcmp(endpoint, "mock") == 0 || starts_with(endpoint, "mock:")) {
        rc = fl_hil_session_from_sim(sess, &sim, err, err_cap);
    } else {
        char *text = NULL;
        if (read_whole_file(endpoint, &text, err, err_cap) != 0) return -1;
        rc = fl_hil_session_from_text(sess, text, err, err_cap);
        free(text);
    }
    if (rc != 0) return -1;
    if (host_sim_out) *host_sim_out = sim;
    return 0;
}

int fl_hil_correlate(const FlHilSession *sess, const FlSim *sim, const char *role,
                     FlHilCorrReport *rep) {
    if (!sess || !sim || !role || !rep || !sess->connected || sim->nhist < 2) {
        if (rep) {
            memset(rep, 0, sizeof *rep);
            snprintf(rep->summary, sizeof rep->summary, "hil correlate: bad args");
        }
        return -1;
    }
    memset(rep, 0, sizeof *rep);

    double bt[FL_HIL_MAX_SAMPLES];
    double bv[FL_HIL_MAX_SAMPLES];
    int nb = 0;
    int i;
    for (i = 0; i < sess->nsamples; i++) {
        if (strcmp(sess->samples[i].role, role) != 0) continue;
        if (nb >= FL_HIL_MAX_SAMPLES) break;
        bt[nb] = sess->samples[i].t_s;
        bv[nb] = sess->samples[i].value;
        nb++;
    }
    if (nb < 2) {
        snprintf(rep->summary, sizeof rep->summary, "hil correlate: need >=2 board samples for %s",
                 role);
        return -1;
    }

    /* Host reference: for imu_az Soft mock uses g0*nz; else zero / control. */
    double sum = 0.0;
    int n = 0;
    for (i = 0; i < sim->nhist; i++) {
        double host;
        if (strcmp(role, "imu_az") == 0)
            host = FL_G0 * (sim->hist[i].nz != 0.0 ? sim->hist[i].nz : 1.0);
        else if (strcmp(role, "radio_throttle") == 0 || strcmp(role, "act_throttle") == 0)
            host = sim->ctrl.throttle;
        else if (strcmp(role, "radio_elevator") == 0 || strcmp(role, "act_elevator") == 0)
            host = sim->ctrl.de_deg;
        else if (strcmp(role, "radio_aileron") == 0 || strcmp(role, "act_aileron") == 0)
            host = sim->ctrl.da_deg;
        else if (strcmp(role, "radio_rudder") == 0 || strcmp(role, "act_rudder") == 0)
            host = sim->ctrl.dr_deg;
        else
            host = 0.0;
        double board = 0.0;
        if (sqn_lerp(bt, bv, nb, sim->hist[i].t, &board) != SQN_OK) continue;
        double d = board - host;
        sum += d * d;
        n++;
    }
    if (n < 2) {
        snprintf(rep->summary, sizeof rep->summary, "hil correlate: lerp produced <2 points");
        return -1;
    }
    rep->npoints = n;
    rep->rms = sqrt(sum / (double)n);
    /* Soft mock is exact → RMS near 0. Tolerate tiny float noise. */
    rep->ok = (rep->rms < 1e-6) ? 1 : 0;
    snprintf(rep->summary, sizeof rep->summary,
             "hil correlate role=%s n=%d rms=%.6g %s (NR Ch.3.1 lerp; Soft mock PCB)", role, n,
             rep->rms, rep->ok ? "PASS" : "FAIL");
    return 0;
}

int fl_hil_report_text(const FlHilSession *sess, const FlHilCorrReport *rep, char *out,
                       int out_cap) {
    if (!sess || !out || out_cap < 32) return -1;
    int used = 0;
    int w = snprintf(out, out_cap,
                     "HIL Soft S7.5: connected=%d board=%s protocol=%s samples=%d\n%s\n",
                     sess->connected, sess->board_id, sess->protocol, sess->nsamples,
                     sess->detail);
    if (w < 0 || w >= out_cap) return -1;
    used = w;
    if (rep) {
        w = snprintf(out + used, out_cap - used, "%s\n", rep->summary);
        if (w < 0 || used + w >= out_cap) return -1;
        used += w;
    }
    w = snprintf(out + used, out_cap - used,
                 "label: provisional, unvalidated against tunnel/flight test\n");
    if (w < 0 || used + w >= out_cap) return -1;
    used += w;
    return used;
}

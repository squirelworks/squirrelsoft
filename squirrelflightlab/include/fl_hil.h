/*
 * Soft S7.5 — Flight Lab HIL against a Soft mock avionics PCB.
 *
 * Soft-owned SQHIL1 line framing (scaffold until Victor freezes a live PCB
 * ICD). CI uses mock:/fixture endpoints; real serial/USB stays refused with
 * a clear message. Channel-time correlation uses sqn_lerp (NR Ch. 3.1 class
 * ideas; Soft original — no NR listings).
 *
 * Does not duplicate squirrelhilbench operator UI.
 */
#ifndef FL_HIL_H
#define FL_HIL_H

#include "fl_avionics.h"
#include "fl_dyn.h"

#define FL_HIL_PROTO "SQHIL1"
#define FL_HIL_MAX_FRAMES 256
#define FL_HIL_MAX_SAMPLES 512 /* enough for ~14 channels × ~36 hist samples */
#define FL_HIL_LINE 160

typedef enum {
    FL_HIL_FR_HELLO = 0,
    FL_HIL_FR_HELLO_ACK,
    FL_HIL_FR_SYNC,
    FL_HIL_FR_CH,
    FL_HIL_FR_BYE,
    FL_HIL_FR_ERROR,
    FL_HIL_FR_UNKNOWN
} FlHilFrameKind;

typedef struct {
    FlHilFrameKind kind;
    char line[FL_HIL_LINE];
    /* Parsed fields (kind-dependent). */
    char role[32];
    int pin;
    double value;
    double t_s;
    int channels; /* HELLO_ACK */
} FlHilFrame;

typedef struct {
    char role[32];
    int pin;
    double t_s;
    double value;
} FlHilSample;

typedef struct {
    char board_id[64];
    char protocol[16];
    int nchannels_ack;
    FlHilSample samples[FL_HIL_MAX_SAMPLES];
    int nsamples;
    int connected;
    char detail[FL_MAX_MSG];
} FlHilSession;

typedef struct {
    int npoints;
    double rms;
    int ok;
    char summary[FL_MAX_MSG];
} FlHilCorrReport;

const char *fl_hil_frame_kind_name(FlHilFrameKind k);

/* Pure framing helpers (unit-tested without hardware). */
int fl_hil_encode_hello(char *out, int out_cap);
int fl_hil_encode_hello_ack(char *out, int out_cap, const char *board_id, int nchannels);
int fl_hil_encode_sync(char *out, int out_cap, double t_s);
int fl_hil_encode_ch(char *out, int out_cap, const char *role, int pin, double value);
int fl_hil_encode_bye(char *out, int out_cap);
int fl_hil_parse_line(const char *line, FlHilFrame *fr);

/* Parse a Soft .sqhil fixture transcript into a session. */
int fl_hil_session_from_text(FlHilSession *sess, const char *text, char *err, int err_cap);

/*
 * Build a Soft mock PCB session from host avionics samples at each history
 * point (deterministic; CI-friendly). Marks sess->connected.
 */
int fl_hil_session_from_sim(FlHilSession *sess, const FlSim *sim, char *err, int err_cap);

/*
 * Connect endpoint:
 *   "mock" / "mock:"*  → Soft mock from default short sim
 *   path to .sqhil     → load fixture transcript
 *   serial/USB path    → refuse (live PCB ICD not frozen)
 * Returns 0 on Soft mock/fixture success.
 */
int fl_hil_connect(const char *endpoint, FlHilSession *sess, FlSim *host_sim_out,
                   char *err, int err_cap);

/*
 * Correlate board samples for role against host FlSensorSample stream over
 * sim history, resampling board onto host times with sqn_lerp (NR Ch. 3.1).
 */
int fl_hil_correlate(const FlHilSession *sess, const FlSim *sim, const char *role,
                     FlHilCorrReport *rep);

/* Format session + correlate summary for CLI. */
int fl_hil_report_text(const FlHilSession *sess, const FlHilCorrReport *rep, char *out,
                       int out_cap);

#endif

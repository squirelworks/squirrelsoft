#ifndef HB_STIMULUS_H
#define HB_STIMULUS_H

#include "hb_profile.h"

#define HB_STIM_NAME_LEN 64
#define HB_STIM_MAX_STEPS 256

typedef struct {
    double t_s;
    char channel[HB_NAME_LEN];
    double value;
} HbStimStep;

typedef struct {
    char name[HB_STIM_NAME_LEN];
    double dt_s;
    double duration_s;
    HbStimStep steps[HB_STIM_MAX_STEPS];
    int nsteps;
} HbStimulus;

typedef struct {
    double t_s;
    double V_mps;
    double alpha_deg;
    double beta_deg;
    double theta_deg;
    double phi_deg;
    double psi_deg;
    double nz;
    double x_m;
    double y_m;
    double z_m;
} HbLogSample;

#define HB_LOG_MAX_SAMPLES 4096

typedef struct {
    char script_name[HB_STIM_NAME_LEN];
    char aircraft_id[HB_NAME_LEN];
    HbLogSample samples[HB_LOG_MAX_SAMPLES];
    int nsamples;
    int ok;
    char summary[HB_MSG_LEN];
} HbStimResult;

int hb_stimulus_parse(HbStimulus *s, const char *text, char *err, int err_cap);

/* Run canned stimulus against a connected mock profile; fill Flight Lab–shaped log. */
int hb_stimulus_run(const HbProfile *p, const HbStimulus *s, HbStimResult *out, char *err,
                    int err_cap);

#endif

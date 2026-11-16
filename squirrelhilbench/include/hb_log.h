#ifndef HB_LOG_H
#define HB_LOG_H

#include "hb_stimulus.h"

/*
 * Flight Lab replay-compatible CSV: same header as fl_sim_history_csv
 * (t_s,V_mps,alpha_deg,beta_deg,theta_deg,phi_deg,psi_deg,nz,x_m,y_m,z_m).
 * Default suggested filename uses the "replay" naming Flight Lab record uses
 * for time-history exports (e.g. hil-stim-replay.csv).
 */
#define HB_REPLAY_CSV_HEADER \
    "t_s,V_mps,alpha_deg,beta_deg,theta_deg,phi_deg,psi_deg,nz,x_m,y_m,z_m\n"

#define HB_DEFAULT_REPLAY_NAME "hil-stim-replay.csv"

/* Format result as Flight Lab–compatible history CSV. Returns bytes or -1. */
int hb_log_format_replay_csv(const HbStimResult *r, char *out, int out_cap);

#endif

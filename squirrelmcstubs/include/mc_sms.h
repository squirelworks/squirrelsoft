/* SMC/SMS stub: station_loadout_flags + safeing display only. */
#ifndef MC_SMS_H
#define MC_SMS_H

#include <stddef.h>

#define MC_STA_MAX 16
#define MC_STA_ID_LEN 16
#define MC_FLAG_LEN 16
#define MC_SMS_MSG 192

typedef struct {
    char station_id[MC_STA_ID_LEN];
    char flag[MC_FLAG_LEN]; /* empty | hung | selected */
} McStationFlag;

typedef struct {
    int mechanical_ground_pins;
    int wow_inhibits_release;
    int dual_consent;
    int fcc_envelope_veto;
    int jettison_guarded;
    int live_fuzing_in_repo; /* must stay 0 in Soft stubs */
    char safe_arm_discrete[MC_FLAG_LEN]; /* safe | disagree | unknown */
} McSafeingDisplay;

typedef struct {
    McStationFlag stations[MC_STA_MAX];
    int nstations;
    McSafeingDisplay safeing;
} McSmsDisplay;

typedef struct {
    int ok;
    char summary[MC_SMS_MSG];
} McSmsTickResult;

int mc_sms_parse_display(McSmsDisplay *out, const char *text, char *err, size_t errlen);

/* One deterministic tick: validate display-only boundary, emit summary. */
void mc_sms_tick(const McSmsDisplay *d, unsigned tick, McSmsTickResult *res);

#endif

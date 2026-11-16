#ifndef HB_HEALTH_H
#define HB_HEALTH_H

#include "hb_profile.h"

typedef struct {
    int n_ok;
    int n_degraded;
    int n_fault;
    int n_offline;
    int n_actuator;
    int n_inceptor;
    int n_bus;
    int estop_ok; /* 1 when estop clear */
    int safeing_safe; /* 1 when not armed */
    int overall_ok;
    char summary[HB_MSG_LEN];
} HbHealthReport;

void hb_health_report(const HbProfile *p, HbHealthReport *out);

/* Format multi-line channel table into out; returns bytes written or -1. */
int hb_health_format(const HbProfile *p, const HbHealthReport *r, char *out, int out_cap);

#endif

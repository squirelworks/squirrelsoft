/*
 * Avionics / hardware co-check: map IMU / radio / actuator channels to
 * simulated sensors and control outputs; flag mismatches vs device doc.
 */
#ifndef FL_AVIONICS_H
#define FL_AVIONICS_H

#include "fl_dyn.h"

typedef struct {
    char role[32];
    int required; /* 1 if the fighter-jet device doc requires it */
    int header_pin;
} FlDeviceExpect;

typedef struct {
    char role[32];
    int ok;
    char detail[FL_MAX_MSG];
} FlChannelFlag;

typedef struct {
    FlChannelFlag flags[FL_MAX_CHANNELS];
    int nflags;
    int nmismatch;
    int nmissing;
} FlAvionicsReport;

/* Built-in expected map for the ISM fighter avionics bay (scaffold). */
int fl_avionics_default_expect(FlDeviceExpect *out, int cap);

/* Compare project channels to expected device-doc roles. */
void fl_avionics_check(const FlProject *p, const FlDeviceExpect *expect, int nexpect,
                       FlAvionicsReport *rep);

/* Sample simulated sensors from the current sim state into channel values. */
typedef struct {
    char role[32];
    double value;
} FlSensorSample;

int fl_avionics_sample(const FlSim *sim, FlSensorSample *out, int cap);

int fl_avionics_report_text(const FlAvionicsReport *rep, char *out, int out_cap);

#endif

#include "hb_health.h"
#include "hb_log.h"
#include "hb_profile.h"
#include "hb_stimulus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return NULL;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

int main(void) {
    char *pt = slurp("fixtures/ism-fighter-hil/bench-profile.ismhil");
    char *st = slurp("fixtures/ism-fighter-hil/stimulus-trim.stim");
    expect(pt && st, "fixtures present");
    if (!pt || !st) return 1;

    HbProfile p;
    char err[128];
    expect(hb_profile_parse(&p, pt, err, sizeof err) == 0, err);
    expect(strcmp(p.aircraft_id, "ISM-FJ-1") == 0, "aircraft_id");
    expect(p.nch >= 10, "channel count");
    expect(hb_profile_connect_mock(&p, err, sizeof err) == 0, err);
    expect(p.connected == 1, "connected");

    HbHealthReport hr;
    hb_health_report(&p, &hr);
    expect(hr.overall_ok == 1, hr.summary);
    expect(hr.n_actuator >= 4 && hr.n_inceptor >= 2 && hr.n_bus >= 2, "kinds");

    HbStimulus stim;
    expect(hb_stimulus_parse(&stim, st, err, sizeof err) == 0, err);
    expect(strcmp(stim.name, "trim_cruise") == 0, "stim name");
    expect(stim.nsteps >= 3, "stim steps");

    HbStimResult res;
    expect(hb_stimulus_run(&p, &stim, &res, err, sizeof err) == 0, err);
    expect(res.ok == 1 && res.nsamples > 5, res.summary);

    char csv[1 << 16];
    expect(hb_log_format_replay_csv(&res, csv, sizeof csv) > 0, "csv format");
    expect(strncmp(csv, HB_REPLAY_CSV_HEADER, strlen(HB_REPLAY_CSV_HEADER)) == 0,
           "Flight Lab replay header");
    expect(strstr(csv, "t_s,V_mps,alpha_deg") == csv, "header columns");

    /* estop blocks connect */
    HbProfile bad = p;
    bad.connected = 0;
    bad.estop_latched = 1;
    expect(hb_profile_connect_mock(&bad, err, sizeof err) != 0, "estop refuse");

    free(pt);
    free(st);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_hilbench ok");
    return 0;
}

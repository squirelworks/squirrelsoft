/* Unit tests for SquirrelFlightLab portable core. */
#include "fl_aero.h"
#include "fl_avionics.h"
#include "fl_dyn.h"
#include "fl_fcs.h"
#include "fl_ism.h"
#include "fl_planned.h"
#include "fl_project.h"
#include "fl_scenario.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
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

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void test_project_roundtrip(void) {
    FlProject p, q;
    fl_project_default_ism_fighter(&p);
    char buf[4096], err[128];
    expect(fl_project_format(&p, buf, sizeof buf) > 0, "format");
    expect(fl_project_parse(&q, buf, err, sizeof err) == 0, err);
    expect(fabs(q.fuselage_length_mm - 420.0) < 1e-6, "fuselage 420");
    expect(q.nsurfaces >= 4, "surfaces");
    expect(fl_project_channel(&q, "imu_ax") != NULL, "imu_ax");
}

static void test_aoa_sweep(void) {
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlAeroModel m;
    fl_aero_from_project(&m, &p);
    FlAoARow rows[32];
    int n = fl_aero_aoa_sweep(&m, 22.0, -2.0, 10.0, 7, 0.0, rows, 32);
    expect(n == 7, "sweep n");
    expect(rows[6].CL > rows[0].CL, "CL rises with alpha");
    FlStabilityDerivs d;
    fl_aero_stability(&m, &d);
    expect(d.static_margin_c > 0.0, "positive static margin stub");
    char csv[4096];
    expect(fl_aero_sweep_csv(rows, n, csv, sizeof csv) > 0, "csv");
    expect(strstr(csv, "alpha_deg,CL") != NULL, "csv header");
}

static void test_dyn_step(void) {
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlSim sim;
    fl_sim_init(&sim, &p);
    FlControls c = {.throttle = 0.6, .de_deg = -2.0, .da_deg = 0, .dr_deg = 0};
    fl_sim_set_controls(&sim, &c);
    int n = fl_sim_run(&sim, 0.5, 0.01, 0.05);
    expect(n > 5, "history length");
    expect(sim.hist[0].t == 0.0, "t0");
    expect(sim.state.t > 0.49, "advanced");
    fl_sim_pause(&sim, 1);
    expect(fl_sim_step(&sim, 0.01) == -1, "paused");
    fl_sim_pause(&sim, 0);
    fl_sim_reset(&sim);
    expect(sim.state.t == 0.0, "reset");
}

static void test_avionics(void) {
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlDeviceExpect expect_map[32];
    int ne = fl_avionics_default_expect(expect_map, 32);
    expect(ne > 10, "expect map");
    FlAvionicsReport rep;
    fl_avionics_check(&p, expect_map, ne, &rep);
    expect(rep.nmissing == 0 && rep.nmismatch == 0, "default project matches doc stub");

    /* Break a pin and expect a flag */
    FlChannel *ch = NULL;
    int i;
    for (i = 0; i < p.nchannels; i++) {
        if (strcmp(p.channels[i].role, "imu_ax") == 0) {
            ch = &p.channels[i];
            break;
        }
    }
    expect(ch != NULL, "find imu_ax");
    if (ch) ch->header_pin = 99;
    fl_avionics_check(&p, expect_map, ne, &rep);
    expect(rep.nmismatch >= 1, "pin mismatch flagged");
}

static void test_scenarios(void) {
    FlScenario list[FL_SCEN_COUNT];
    int n = 0;
    fl_scenario_defaults(list, &n);
    expect(n == 3, "three scenarios");
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlSim sim;
    fl_sim_init(&sim, &p);
    int i;
    for (i = 0; i < n; i++) {
        FlScenarioResult res;
        expect(fl_scenario_run(&sim, &list[i], &res) == 0, list[i].name);
        /* Scaffold thresholds are tuned so the default model can pass. */
        expect(res.passed == 1, list[i].name);
    }
}

static void test_planned(void) {
    int n = 0;
    const FlPlannedSurface *list = fl_planned_list(&n);
    expect(n == 3, "three planned");
    expect(strcmp(list[0].status, "planned") == 0, "status");
    char err[128];
    expect(fl_planned_cfd_mesh_import("x.msh", err, sizeof err) == -1, "cfd stub");
    expect(strstr(err, "not implemented") != NULL, "cfd msg");
}

static void test_ism_and_fcs(void) {
    char *params = slurp("fixtures/ism-fighter/ism-fj-1.ismfj-params");
    char *iface = slurp("fixtures/ism-fighter/bay-io.ismfj-iface");
    expect(params != NULL && iface != NULL, "fixture files");
    if (!params || !iface) {
        free(params);
        free(iface);
        return;
    }
    FlProject p;
    FlIsmMeta meta;
    char err[128];
    expect(fl_ism_parse_params(&p, &meta, params, err, sizeof err) == 0, err);
    expect(fl_ism_parse_interface(&p, &meta, iface, err, sizeof err) == 0, err);
    expect(fabs(p.fuselage_length_mm - 420.0) < 1e-6, "ISM L=420");
    expect(fabs(p.wing_sweep_deg - 35.0) < 1e-6, "ISM sweep=35");
    expect(fl_project_channel(&p, "act_aileron") != NULL, "act_aileron");
    FlIsmVerify v;
    fl_ism_verify_project(&p, &meta, &v);
    expect(v.ok == 1, v.summary);
    FlSim sim;
    fl_sim_init(&sim, &p);
    FlFcsLimits lim;
    fl_fcs_limits_default(&lim);
    FlFcsResult res;
    expect(fl_fcs_co_check(&sim, &lim, &res) == 0, "fcs run");
    expect(res.passed == 1, res.summary);
    free(params);
    free(iface);
}

int main(void) {
    test_project_roundtrip();
    test_aoa_sweep();
    test_dyn_step();
    test_avionics();
    test_scenarios();
    test_planned();
    test_ism_and_fcs();
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_flightlab ok");
    return 0;
}

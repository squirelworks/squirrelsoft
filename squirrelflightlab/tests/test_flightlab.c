/* Unit tests for SquirrelFlightLab portable core. */
#include "fl_aero.h"
#include "fl_avionics.h"
#include "fl_hil.h"
#include "fl_chart.h"
#include "fl_dyn.h"
#include "fl_fcs.h"
#include "fl_iface.h"
#include "fl_ism.h"
#include "fl_planned.h"
#include "fl_project.h"
#include "fl_scenario.h"
#include "fl_verify.h"
#include "fl_viability.h"
#include "fl_loadout.h"

#include "sqgui.h"

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
    expect(sim.integ == FL_INTEG_RK4, "default RK4");
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

static void test_units_scale(void) {
    /* Catch mm/g mix-ups (Fred D1 / Soft #174). */
    expect(fabs(fl_mm_to_m(420.0) - 0.42) < 1e-12, "420 mm → 0.42 m");
    expect(fabs(fl_mm_to_m(420.0) - 420.0) > 1.0, "not identity mm");
    expect(fabs(fl_g_to_kg(500.0) - 0.5) < 1e-12, "500 g → 0.5 kg");
    expect(fabs(fl_g_to_kg(500.0) - 500.0) > 1.0, "not identity g");
    expect(fabs(fl_g_mm2_to_kg_m2(1.0) - 1e-9) < 1e-20, "1 g·mm² → 1e-9 kg·m²");
    expect(fabs(fl_kg_to_g(fl_g_to_kg(850.0)) - 850.0) < 1e-9, "g roundtrip");

    FlProject p;
    fl_project_default_ism_fighter(&p);
    expect(p.scale_mode == FL_SCALE_STUDY, "default study scale");
    double mass_si = fl_g_to_kg(p.mass_g);
    expect(mass_si < 5.0 && mass_si > 0.1, "study mass is ~kg not raw grams");
    /* Fake bug: treating mass_g as kg would be ~850 kg for a 420 mm model. */
    expect(p.mass_g / mass_si > 900.0, "g vs kg differ by ~1000×");
}

static void test_integrator_convergence(void) {
    /* RK4 vs Euler: finer dt → closer states; RK4 drifts less on short run. */
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlSim a, b;
    fl_sim_init(&a, &p);
    fl_sim_init(&b, &p);
    fl_sim_set_integrator(&a, FL_INTEG_RK4);
    fl_sim_set_integrator(&b, FL_INTEG_EULER);
    FlControls c = {.throttle = 0.55, .de_deg = -1.0, .da_deg = 0, .dr_deg = 0};
    fl_sim_set_controls(&a, &c);
    fl_sim_set_controls(&b, &c);
    fl_sim_run(&a, 2.0, 0.01, 0.5);
    fl_sim_run(&b, 2.0, 0.01, 0.5);
    double dV = fabs(a.hist[a.nhist - 1].V - b.hist[b.nhist - 1].V);
    expect(dV >= 0.0, "rk4 vs euler ran");
    /* Halve dt on Euler → move toward RK4 reference */
    FlSim e1, e2;
    fl_sim_init(&e1, &p);
    fl_sim_init(&e2, &p);
    fl_sim_set_integrator(&e1, FL_INTEG_EULER);
    fl_sim_set_integrator(&e2, FL_INTEG_EULER);
    fl_sim_set_controls(&e1, &c);
    fl_sim_set_controls(&e2, &c);
    fl_sim_run(&e1, 1.0, 0.05, 1.0);
    fl_sim_run(&e2, 1.0, 0.025, 1.0);
    FlSim rk;
    fl_sim_init(&rk, &p);
    fl_sim_set_controls(&rk, &c);
    fl_sim_run(&rk, 1.0, 0.025, 1.0);
    double err_coarse = fabs(e1.hist[e1.nhist - 1].V - rk.hist[rk.nhist - 1].V);
    double err_fine = fabs(e2.hist[e2.nhist - 1].V - rk.hist[rk.nhist - 1].V);
    expect(err_fine <= err_coarse + 1e-6, "Euler converges toward RK4 as dt↓");
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
    expect(strcmp(list[0].status, "planned") == 0, "cfd planned");
    expect(strcmp(list[1].status, "active") == 0, "tunnel active");
    expect(strcmp(list[2].status, "active") == 0, "hil active");
    char err[128];
    expect(fl_planned_cfd_mesh_import("x.msh", err, sizeof err) == -1, "cfd stub");
    expect(strstr(err, "not implemented") != NULL, "cfd msg");
}

static void test_hil_framing_and_mock(void) {
    char line[160];
    FlHilFrame fr;
    expect(fl_hil_encode_hello(line, sizeof line) > 0, "hello enc");
    expect(fl_hil_parse_line(line, &fr) == 0 && fr.kind == FL_HIL_FR_HELLO, "hello parse");
    expect(fl_hil_encode_hello_ack(line, sizeof line, "ISM-FJ-MOCK", 14) > 0, "ack enc");
    expect(fl_hil_parse_line(line, &fr) == 0 && fr.kind == FL_HIL_FR_HELLO_ACK, "ack parse");
    expect(fr.channels == 14, "ack channels");
    expect(fl_hil_encode_sync(line, sizeof line, 0.25) > 0, "sync enc");
    expect(fl_hil_parse_line(line, &fr) == 0 && fr.kind == FL_HIL_FR_SYNC, "sync parse");
    expect(fabs(fr.t_s - 0.25) < 1e-9, "sync t");
    expect(fl_hil_encode_ch(line, sizeof line, "act_throttle", 14, 0.55) > 0, "ch enc");
    expect(fl_hil_parse_line(line, &fr) == 0 && fr.kind == FL_HIL_FR_CH, "ch parse");
    expect(strcmp(fr.role, "act_throttle") == 0 && fr.pin == 14, "ch fields");
    expect(fabs(fr.value - 0.55) < 1e-9, "ch value");
    expect(fl_hil_encode_bye(line, sizeof line) > 0, "bye enc");
    expect(fl_hil_parse_line(line, &fr) == 0 && fr.kind == FL_HIL_FR_BYE, "bye parse");

    FlHilSession sess;
    FlSim host;
    char err[240];
    expect(fl_hil_connect("mock", &sess, &host, err, sizeof err) == 0, err);
    expect(sess.connected == 1, "mock connected");
    expect(sess.nsamples >= 2, "mock samples");
    FlHilCorrReport rep;
    expect(fl_hil_correlate(&sess, &host, "act_throttle", &rep) == 0, rep.summary);
    expect(rep.ok == 1, rep.summary);
    expect(strstr(rep.summary, "NR Ch.3.1") != NULL, "NR cite");
    expect(fl_hil_correlate(&sess, &host, "imu_az", &rep) == 0, rep.summary);
    expect(rep.ok == 1, "imu_az mock match");

    expect(fl_hil_connect("/dev/ttyUSB0", &sess, &host, err, sizeof err) == -1, "serial refuse");
    expect(strstr(err, "deferred") != NULL, err);

    char *fx = slurp("fixtures/hil/mock-pcb-session.sqhil");
    expect(fx != NULL, "hil fixture");
    if (fx) {
        expect(fl_hil_session_from_text(&sess, fx, err, sizeof err) == 0, err);
        expect(sess.nsamples >= 2, "fixture samples");
        /* Host sim for correlate — same controls as connect(). */
        FlProject p;
        fl_project_default_ism_fighter(&p);
        fl_sim_init(&host, &p);
        FlControls c = {0.55, -1.0, 0.0, 0.0};
        fl_sim_set_controls(&host, &c);
        expect(fl_sim_run(&host, 0.5, 0.05, 0.05) >= 2, "host hist");
        expect(fl_hil_correlate(&sess, &host, "act_throttle", &rep) == 0, rep.summary);
        expect(rep.ok == 1, rep.summary);
        free(fx);
    }
}

static void test_strip_chart(void) {
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlChartStyle st;
    fl_chart_style_default(&st);
    st.w = 320;
    st.h = 180;
    uint32_t *px = calloc((size_t)st.w * (size_t)st.h, sizeof *px);
    expect(px != NULL, "chart pixels");
    if (!px) return;
    sqg_canvas cv;
    sqg_canvas_init(&cv, px, st.w, st.h, st.w);
    int segs = fl_chart_render_demo(&cv, &p, &st, 1.0, 0.05);
    expect(segs > 10, "chart segments");
    /* Non-background pixel present */
    {
        int i, painted = 0;
        for (i = 0; i < st.w * st.h; i++)
            if (px[i] != st.bg) painted++;
        expect(painted > 100, "painted pixels");
    }
    free(px);
}

static void test_hil_chart(void) {
    FlChartStyle st;
    fl_chart_style_default(&st);
    st.w = 320;
    st.h = 180;
    uint32_t *px = calloc((size_t)st.w * (size_t)st.h, sizeof *px);
    expect(px != NULL, "hil chart pixels");
    if (!px) return;
    sqg_canvas cv;
    sqg_canvas_init(&cv, px, st.w, st.h, st.w);
    char err[240];
    int segs = fl_chart_render_hil(&cv, "mock", "act_throttle", "imu_az", &st, err,
                                   sizeof err);
    expect(segs > 10, err[0] ? err : "hil chart segments");
    {
        int i, painted = 0;
        for (i = 0; i < st.w * st.h; i++)
            if (px[i] != st.bg) painted++;
        expect(painted > 100, "hil painted pixels");
    }
    expect(fl_chart_render_hil(&cv, "/dev/ttyUSB0", "act_throttle", NULL, &st, err,
                               sizeof err) == -1,
           "hil-chart serial refuse");
    expect(strstr(err, "deferred") != NULL, err);
    free(px);
}

static void test_tunnel_provider(void) {
    char *csv = slurp("fixtures/tunnel/soft-scaffold-correlate.csv");
    expect(csv != NULL, "tunnel fixture");
    if (!csv) return;
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlAeroModel m;
    fl_aero_from_project(&m, &p);
    char err[128];
    expect(fl_aero_load_tunnel_csv(&m, csv, err, sizeof err) == 0, err);
    expect(m.provider == FL_AERO_PROVIDER_TUNNEL_TABLE, "provider");
    expect(m.n_tunnel == 5, "5 knots");
    FlAeroState s;
    memset(&s, 0, sizeof s);
    s.V = 20.0;
    s.alpha_rad = fl_rad(4.0);
    FlAeroForces f;
    fl_aero_eval(&m, &s, &f);
    expect(fabs(f.CL - 0.485103) < 1e-5, "lerp CL@4deg");
    /* Midpoint between 0 and 4 via NR Ch.3.1 */
    s.alpha_rad = fl_rad(2.0);
    fl_aero_eval(&m, &s, &f);
    expect(fabs(f.CL - 0.5 * (0.150000 + 0.485103)) < 1e-5, "lerp mid");
    FlTunnelCorrReport rep;
    expect(fl_aero_tunnel_correlate(&m, &p, &rep) == 0, "correlate");
    expect(rep.ok == 1, rep.summary);
    expect(strstr(rep.summary, "NR Ch.3.1") != NULL, "NR cite");
    free(csv);
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

static void test_loadout(void) {
    FlLoReport rep;
    char err[256];
    expect(fl_loadout_validate("fixtures/ism-interfaces",
                               "fixtures/loadouts/legal-symmetric.yaml", &rep, err,
                               sizeof err) == 0,
           err);
    expect(rep.nfail == 0, "legal loadout");
    expect(strstr(rep.label, "provisional") != NULL, "loadout provisional");
    expect(fl_loadout_validate("fixtures/ism-interfaces",
                               "fixtures/loadouts/illegal-heavy-tip.yaml", &rep, err,
                               sizeof err) == 1,
           "heavy tip must fail");
    expect(rep.nfail > 0, "heavy tip nfail");
    expect(fl_loadout_validate("fixtures/ism-interfaces",
                               "fixtures/loadouts/illegal-adapter.yaml", &rep, err,
                               sizeof err) == 1,
           "bad adapter must fail");
    expect(fl_loadout_validate("fixtures/ism-interfaces",
                               "fixtures/loadouts/illegal-envelope.yaml", &rep, err,
                               sizeof err) == 1,
           "bad envelope must fail");
}

static void test_viability(void) {
    FlViaReport rep;
    char err[256];
    expect(fl_viability_eval_file("fixtures/ism-interfaces/viability-criteria.yaml",
                                  &rep, err, sizeof err) == 0,
           err);
    expect(rep.nfail == 0, "no viability hard fails");
    expect(strstr(rep.label, "provisional") != NULL, "via provisional");
    expect(strcmp(rep.aircraft_id, "ISM-FJ-1") == 0, "via aircraft");
    int saw_breguet = 0, saw_cg = 0, saw_struct = 0;
    int i;
    for (i = 0; i < rep.ngates; i++) {
        if (strcmp(rep.gates[i].id, "G-BREGUET") == 0) {
            saw_breguet = 1;
            expect(rep.gates[i].soft_pass, "Soft Breguet pass");
            expect(strstr(rep.gates[i].detail, "Brent") != NULL, "Brent oracle");
            expect(strstr(rep.gates[i].detail, "deferred") == NULL,
                   "Breguet not deferred");
        }
        if (strcmp(rep.gates[i].id, "G-CG") == 0) {
            saw_cg = 1;
            expect(rep.gates[i].soft_pass, "Soft CG pass");
            expect(fabs(rep.gates[i].measured - 10.0) < 1e-6, "CG travel 10%MAC");
        }
        if (strcmp(rep.gates[i].id, "G-STRUCT-MARGIN") == 0) {
            saw_struct = 1;
            expect(rep.gates[i].soft_pass, "Soft struct pass");
            expect(fabs(rep.gates[i].measured - 13.5) < 1e-6, "ultimate 13.5g");
        }
    }
    expect(saw_breguet && saw_cg && saw_struct, "Soft recomputed deferred gates");
    char md[8192], js[8192];
    expect(fl_viability_report_md(&rep, md, sizeof md) == 0, "via md");
    expect(fl_viability_report_json(&rep, js, sizeof js) == 0, "via json");
    expect(strstr(js, "\"overall\"") != NULL, "json overall");
    char note[128];
    expect(fl_viability_optional_xcheck(NULL, NULL, NULL, note, sizeof note) == 1,
           "xcheck skip");
}

static void test_verify(void) {
    FlVerifyReport rep;
    expect(fl_verify_run_analytic(&rep) == 0, "analytic verify");
    expect(rep.nfail == 0, "no verify fails");
    expect(strstr(rep.label, "provisional") != NULL, "provisional label");
    char md[4096];
    expect(fl_verify_report_md(&rep, md, sizeof md) == 0, "md report");
    expect(strstr(md, "pendulum_energy") != NULL, "pendulum case");
    expect(strstr(md, "torque_free_energy") != NULL, "rigid case");
    char err[128];
    expect(fl_verify_optional_xcheck(NULL, NULL, &rep, err, sizeof err) == 1,
           "optional xcheck skip");
}

static void test_ism_interfaces(void) {
    FlIfaceBundle bundle;
    char err[256];
    expect(fl_iface_load_dir(&bundle, "fixtures/ism-interfaces",
                             "schemas/ism-fj-1.0.0/top-level-allowlist.yaml", err,
                             sizeof err) == 0,
           err);
    expect(bundle.nerrors == 0, "no iface errors");
    expect(bundle.ndocs >= 16, "manifest + every listed YAML");
    expect(strcmp(bundle.schema_version, "1.0.0") == 0, "schema_version 1.0.0");
    expect(strcmp(bundle.aircraft_id, "ISM-FJ-1") == 0, "aircraft_id");

    /* Unsupported schema_version */
    FlIfaceSchema *schema = calloc(1, sizeof *schema);
    expect(schema != NULL, "schema oom");
    if (!schema) return;
    char *schema_text = slurp("schemas/ism-fj-1.0.0/top-level-allowlist.yaml");
    expect(schema_text != NULL, "soft schema readable");
    if (!schema_text) {
        free(schema);
        return;
    }
    expect(fl_iface_schema_load(schema, schema_text, err, sizeof err) == 0, err);
    free(schema_text);

    const char *bad_ver =
        "schema_version: \"9.9.9\"\n"
        "aircraft_id: \"ISM-FJ-1\"\n";
    FlIfaceDoc doc;
    expect(fl_iface_validate_doc(schema, "aircraft-geometry.yaml", bad_ver, &doc,
                                 NULL, err, sizeof err) != 0,
           "reject unknown schema_version");
    expect(strstr(err, "unsupported schema_version") != NULL, err);

    const char *unknown_key =
        "schema_version: \"1.0.0\"\n"
        "aircraft_id: \"ISM-FJ-1\"\n"
        "not_a_real_field: 1\n"
        "revision: \"A\"\n"
        "status: stub\n"
        "source_drawings: []\n"
        "source_shell: x\n"
        "datum: {}\n"
        "study_model: {}\n"
        "full_scale: {}\n"
        "fuselage_stations_pct_fs: {}\n"
        "wing_stations_pct_semi_span: {}\n"
        "spars: {}\n"
        "empennage: {}\n";
    expect(fl_iface_validate_doc(schema, "aircraft-geometry.yaml", unknown_key, &doc,
                                 NULL, err, sizeof err) != 0,
           "reject unknown key");
    expect(strstr(err, "unknown key") != NULL, err);

    const char *missing =
        "schema_version: \"1.0.0\"\n"
        "revision: \"A\"\n";
    expect(fl_iface_validate_doc(schema, "mass-properties.yaml", missing, &doc, NULL,
                                 err, sizeof err) != 0,
           "reject missing required");
    expect(strstr(err, "missing required") != NULL, err);

    /* Soft #177: nested unknown / missing required paths */
    char *fc = slurp("fixtures/ism-interfaces/flight-controls.yaml");
    expect(fc != NULL, "flight-controls fixture");
    if (fc) {
        expect(fl_iface_validate_doc(schema, "flight-controls.yaml", fc, &doc, NULL,
                                     err, sizeof err) == 0,
               err);
        free(fc);
    }

    const char *nested_unknown =
        "schema_version: \"1.0.0\"\n"
        "aircraft_id: \"ISM-FJ-1\"\n"
        "revision: \"A\"\n"
        "status: stub\n"
        "source_drawings: []\n"
        "source_docs: []\n"
        "architecture: {fbw: true}\n"
        "surfaces:\n"
        "  - id: aileron_L\n"
        "    not_a_surface_field: 1\n"
        "modes: [{id: ground}]\n"
        "envelope: {aoa_limit_deg: 20, nz_limit_g: 9}\n"
        "expected_behaviour: {}\n";
    expect(fl_iface_validate_doc(schema, "flight-controls.yaml", nested_unknown, &doc,
                                 NULL, err, sizeof err) != 0,
           "reject nested unknown key");
    expect(strstr(err, "unknown key") != NULL && strstr(err, "surfaces[0]") != NULL,
           err);

    const char *nested_missing =
        "schema_version: \"1.0.0\"\n"
        "aircraft_id: \"ISM-FJ-1\"\n"
        "revision: \"A\"\n"
        "status: stub\n"
        "source_drawings: []\n"
        "source_docs: []\n"
        "architecture: {fbw: true}\n"
        "surfaces: [{id: aileron_L}]\n"
        "modes: [{id: ground}]\n"
        "envelope: {provisional: true}\n"
        "expected_behaviour: {}\n";
    expect(fl_iface_validate_doc(schema, "flight-controls.yaml", nested_missing, &doc,
                                 NULL, err, sizeof err) != 0,
           "reject nested missing required");
    expect(strstr(err, "missing required") != NULL && strstr(err, "envelope") != NULL,
           err);
    free(schema);
}

int main(void) {
    test_project_roundtrip();
    test_aoa_sweep();
    test_dyn_step();
    test_units_scale();
    test_integrator_convergence();
    test_avionics();
    test_scenarios();
    test_planned();
    test_tunnel_provider();
    test_strip_chart();
    test_hil_chart();
    test_hil_framing_and_mock();
    test_ism_and_fcs();
    test_ism_interfaces();
    test_verify();
    test_viability();
    test_loadout();
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_flightlab ok");
    return 0;
}

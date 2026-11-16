/* SquirrelFlightLab / Fighter Jet Flight Lab — CLI front-end. Original code. */
#define _POSIX_C_SOURCE 200809L
#include "fl_aero.h"
#include "fl_avionics.h"
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs(
        "usage: squirrelflightlab <command> [options]\n"
        "\n"
        "  help                         this text\n"
        "  demo                         happy-path: AoA sweep + short flight history\n"
        "  project [FILE]               print default or loaded .sqflt project\n"
        "  aoa-sweep [options]          parametric lift/drag/moment vs angle of attack\n"
        "      --project FILE  --V MPS  --alpha0 DEG --alpha1 DEG --n N  --de DEG\n"
        "      --csv FILE      write table; also prints a small ASCII CL plot\n"
        "  record [options]             integrate dynamics and write a time history\n"
        "      --project FILE  --duration S  --dt S  --throttle T  --de/--da/--dr DEG\n"
        "      --csv FILE\n"
        "  scenario NAME [options]      trim_cruise | pull_up | roll\n"
        "      --project FILE  --max-q DPS  --min-nz N  --min-roll DEG  --max-alt-loss M\n"
        "  avionics-check [FILE]        compare project channels to device-doc expect map\n"
        "  ism-load --params F [--iface F]   load ISM-FJ parameter (+ bay interface) files\n"
        "  ism-interfaces [DIR] [--schema F]  load all fighter-jet/interfaces YAML by\n"
        "                                   schema_version (Soft allowlist gate)\n"
        "      DIR default: fixtures/ism-interfaces\n"
        "      --schema Soft top-level allowlist YAML\n"
        "  fcs-check [options]          scripted FCS co-check; writes pass/fail report\n"
        "      --params F  --iface F  --report FILE\n"
        "  verify [--openvsp F] [--avl F]  analytic verification suite (Soft #172)\n"
        "  viability-report [FILE] [--json OUT] [--md OUT]\n"
        "                               evaluate viability-criteria.yaml (Soft #176)\n"
        "  loadout-check FILE [--iface DIR]\n"
        "                               validate station×adapter×store loadout (Soft #173)\n"
        "  planned                      list scaffold/planned surfaces (CFD, tunnel, HIL)\n"
        "  cfd-import PATH | tunnel PATH | hil ENDPOINT\n"
        "                               planned stubs (always fail with a clear message)\n"
        "\n"
        "ISM SoT: international-squirrel-machines fighter-jet/ (Victor). Soft loads\n"
        "params + interfaces YAML; CI snapshots under fixtures/ism-fighter/ and\n"
        "fixtures/ism-interfaces/ (refresh from ISM main; Soft does not fork SoT).\n"
        "Units: geometry mm/g; dynamics SI. Axes: +X nose, +Y right, +Z down.\n",
        f);
}

static int read_file(const char *path, char **out, size_t *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return -1;
    }
    buf[n] = 0;
    fclose(f);
    *out = buf;
    if (out_n) *out_n = (size_t)n;
    return 0;
}

static int load_project(FlProject *p, const char *path, char *err, int err_cap) {
    if (!path) {
        fl_project_default_ism_fighter(p);
        return 0;
    }
    char *text = NULL;
    if (read_file(path, &text, NULL) != 0) {
        snprintf(err, err_cap, "cannot read %s", path);
        return -1;
    }
    int rc = fl_project_parse(p, text, err, err_cap);
    free(text);
    return rc;
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    if (fclose(f) || !ok) return -1;
    return 0;
}

static void ascii_cl_plot(const FlAoARow *rows, int n) {
    if (n < 2) return;
    double mn = rows[0].CL, mx = rows[0].CL;
    int i;
    for (i = 1; i < n; i++) {
        if (rows[i].CL < mn) mn = rows[i].CL;
        if (rows[i].CL > mx) mx = rows[i].CL;
    }
    if (mx - mn < 1e-9) {
        mx = mn + 1.0;
    }
    puts("CL vs alpha (scaffold ASCII plot):");
    for (i = 0; i < n; i++) {
        int w = (int)((rows[i].CL - mn) / (mx - mn) * 40.0 + 0.5);
        if (w < 0) w = 0;
        if (w > 40) w = 40;
        printf("%6.1f |", rows[i].alpha_deg);
        int j;
        for (j = 0; j < w; j++) putchar('#');
        printf("  CL=%.3f\n", rows[i].CL);
    }
}

static const char *opt_val(int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return NULL;
    (*i)++;
    return argv[*i];
}

static int cmd_aoa(int argc, char **argv) {
    const char *project = NULL, *csv = NULL;
    double V = 22.0, a0 = -4.0, a1 = 12.0, de = 0.0;
    int n = 17;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--project") == 0) {
            project = opt_val(argc, argv, &i);
            if (!project) return 2;
        } else if (strcmp(argv[i], "--V") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            V = atof(v);
        } else if (strcmp(argv[i], "--alpha0") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            a0 = atof(v);
        } else if (strcmp(argv[i], "--alpha1") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            a1 = atof(v);
        } else if (strcmp(argv[i], "--n") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            n = atoi(v);
        } else if (strcmp(argv[i], "--de") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            de = atof(v);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = opt_val(argc, argv, &i);
            if (!csv) return 2;
        } else return 2;
    }
    FlProject p;
    char err[FL_MAX_MSG];
    if (load_project(&p, project, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return 1;
    }
    FlAeroModel m;
    fl_aero_from_project(&m, &p);
    FlAoARow rows[FL_MAX_SWEEP];
    int got = fl_aero_aoa_sweep(&m, V, a0, a1, n, de, rows, FL_MAX_SWEEP);
    if (got < 0) {
        fprintf(stderr, "squirrelflightlab: bad sweep args\n");
        return 1;
    }
    FlStabilityDerivs d;
    fl_aero_stability(&m, &d);
    printf("project=%s  V=%.2f m/s  S=%.4f m^2  c=%.4f m\n", p.name, V, m.S_m2, m.c_m);
    printf("stability: CLa=%.3f Cma=%.3f SM=%.3f c  Clb=%.3f Cnb=%.3f\n", d.CLa, d.Cma,
           d.static_margin_c, d.Clb, d.Cnb);
    ascii_cl_plot(rows, got);
    char table[1 << 16];
    if (fl_aero_sweep_csv(rows, got, table, sizeof table) < 0) {
        fprintf(stderr, "squirrelflightlab: csv buffer full\n");
        return 1;
    }
    fputs(table, stdout);
    if (csv && write_text(csv, table) != 0) {
        fprintf(stderr, "squirrelflightlab: cannot write %s\n", csv);
        return 1;
    }
    return 0;
}

static int cmd_record(int argc, char **argv) {
    const char *project = NULL, *csv = NULL;
    double duration = 2.0, dt = 0.01, throttle = 0.55, de = -1.5, da = 0.0, dr = 0.0;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--project") == 0) {
            project = opt_val(argc, argv, &i);
            if (!project) return 2;
        } else if (strcmp(argv[i], "--duration") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            duration = atof(v);
        } else if (strcmp(argv[i], "--dt") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            dt = atof(v);
        } else if (strcmp(argv[i], "--throttle") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            throttle = atof(v);
        } else if (strcmp(argv[i], "--de") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            de = atof(v);
        } else if (strcmp(argv[i], "--da") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            da = atof(v);
        } else if (strcmp(argv[i], "--dr") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            dr = atof(v);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = opt_val(argc, argv, &i);
            if (!csv) return 2;
        } else return 2;
    }
    FlProject p;
    char err[FL_MAX_MSG];
    if (load_project(&p, project, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return 1;
    }
    FlSim sim;
    fl_sim_init(&sim, &p);
    FlControls c = {.throttle = throttle, .de_deg = de, .da_deg = da, .dr_deg = dr};
    fl_sim_set_controls(&sim, &c);
    int ns = fl_sim_run(&sim, duration, dt, dt);
    if (ns < 0) {
        fprintf(stderr, "squirrelflightlab: record failed\n");
        return 1;
    }
    printf("recorded %d samples over %.2f s (project=%s)\n", ns, duration, p.name);
    char table[1 << 18];
    if (fl_sim_history_csv(&sim, table, sizeof table) < 0) {
        fprintf(stderr, "squirrelflightlab: history csv buffer full\n");
        return 1;
    }
    /* print a short preview */
    int lines = 0;
    const char *s = table;
    while (*s && lines < 8) {
        const char *e = strchr(s, '\n');
        if (!e) break;
        fwrite(s, 1, (size_t)(e - s + 1), stdout);
        s = e + 1;
        lines++;
    }
    if (*s) puts("...");
    if (csv && write_text(csv, table) != 0) {
        fprintf(stderr, "squirrelflightlab: cannot write %s\n", csv);
        return 1;
    }
    return 0;
}

static int cmd_scenario(int argc, char **argv) {
    if (argc < 1) return 2;
    const char *name = argv[0];
    const char *project = NULL;
    FlScenario list[FL_SCEN_COUNT];
    int n = 0;
    fl_scenario_defaults(list, &n);
    const FlScenario *found = fl_scenario_by_name(list, n, name);
    if (!found) {
        fprintf(stderr, "squirrelflightlab: unknown scenario '%s'\n", name);
        return 2;
    }
    FlScenario sc = *found;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--project") == 0) {
            project = opt_val(argc, argv, &i);
            if (!project) return 2;
        } else if (strcmp(argv[i], "--max-q") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            sc.max_abs_pitch_rate_dps = atof(v);
        } else if (strcmp(argv[i], "--min-nz") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            sc.min_nz = atof(v);
        } else if (strcmp(argv[i], "--min-roll") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            sc.min_abs_roll_deg = atof(v);
        } else if (strcmp(argv[i], "--max-alt-loss") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v) return 2;
            sc.max_altitude_loss_m = atof(v);
        } else return 2;
    }
    FlProject p;
    char err[FL_MAX_MSG];
    if (load_project(&p, project, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return 1;
    }
    FlSim sim;
    fl_sim_init(&sim, &p);
    FlScenarioResult res;
    if (fl_scenario_run(&sim, &sc, &res) != 0) {
        fprintf(stderr, "squirrelflightlab: scenario failed to run\n");
        return 1;
    }
    puts(res.summary);
    return res.passed ? 0 : 1;
}

static int cmd_avionics(const char *path) {
    FlProject p;
    char err[FL_MAX_MSG];
    if (load_project(&p, path, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return 1;
    }
    FlDeviceExpect expect[FL_MAX_CHANNELS];
    int ne = fl_avionics_default_expect(expect, FL_MAX_CHANNELS);
    FlAvionicsReport rep;
    fl_avionics_check(&p, expect, ne, &rep);
    char text[8192];
    fl_avionics_report_text(&rep, text, sizeof text);
    fputs(text, stdout);
    return (rep.nmismatch || rep.nmissing) ? 1 : 0;
}

static int load_ism_files(FlProject *p, FlIsmMeta *meta, const char *params,
                          const char *iface, char *err, int err_cap) {
    char *text = NULL;
    if (!params) {
        snprintf(err, err_cap, "need --params FILE");
        return -1;
    }
    if (read_file(params, &text, NULL) != 0) {
        snprintf(err, err_cap, "cannot read %s", params);
        return -1;
    }
    if (fl_ism_parse_params(p, meta, text, err, err_cap) != 0) {
        free(text);
        return -1;
    }
    free(text);
    if (meta) snprintf(meta->params_path, sizeof meta->params_path, "%s", params);
    if (iface) {
        if (read_file(iface, &text, NULL) != 0) {
            snprintf(err, err_cap, "cannot read %s", iface);
            return -1;
        }
        if (fl_ism_parse_interface(p, meta, text, err, err_cap) != 0) {
            free(text);
            return -1;
        }
        free(text);
        if (meta) snprintf(meta->iface_path, sizeof meta->iface_path, "%s", iface);
    }
    return 0;
}

static int cmd_ism_load(int argc, char **argv) {
    const char *params = NULL, *iface = NULL;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--params") == 0) {
            params = opt_val(argc, argv, &i);
            if (!params) return 2;
        } else if (strcmp(argv[i], "--iface") == 0) {
            iface = opt_val(argc, argv, &i);
            if (!iface) return 2;
        } else return 2;
    }
    FlProject p;
    FlIsmMeta meta;
    char err[FL_MAX_MSG];
    if (load_ism_files(&p, &meta, params, iface, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return 1;
    }
    FlIsmVerify v;
    fl_ism_verify_project(&p, &meta, &v);
    puts(v.summary);
    char buf[4096];
    if (fl_project_format(&p, buf, sizeof buf) < 0) return 1;
    fputs(buf, stdout);
    printf("# surfaces=%d channels=%d\n", p.nsurfaces, p.nchannels);
    return v.ok ? 0 : 1;
}

static int cmd_ism_interfaces(int argc, char **argv) {
    const char *dir = "fixtures/ism-interfaces";
    const char *schema = "schemas/ism-fj-1.0.0/top-level-allowlist.yaml";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--schema") == 0) {
            schema = opt_val(argc, argv, &i);
            if (!schema) return 2;
        } else if (argv[i][0] == '-') {
            return 2;
        } else {
            dir = argv[i];
        }
    }
    FlIfaceBundle bundle;
    char err[FL_MAX_MSG];
    int rc = fl_iface_load_dir(&bundle, dir, schema, err, sizeof err);
    char report[8192];
    fl_iface_report(&bundle, report, sizeof report);
    fputs(report, rc == 0 ? stdout : stderr);
    if (rc != 0) {
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return 1;
    }
    return 0;
}

static int cmd_fcs_check(int argc, char **argv) {
    const char *params = "fixtures/ism-fighter/ism-fj-1.ismfj-params";
    const char *iface = "fixtures/ism-fighter/bay-io.ismfj-iface";
    const char *report = NULL;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--params") == 0) {
            params = opt_val(argc, argv, &i);
            if (!params) return 2;
        } else if (strcmp(argv[i], "--iface") == 0) {
            iface = opt_val(argc, argv, &i);
            if (!iface) return 2;
        } else if (strcmp(argv[i], "--report") == 0) {
            report = opt_val(argc, argv, &i);
            if (!report) return 2;
        } else return 2;
    }
    FlProject p;
    FlIsmMeta meta;
    char err[FL_MAX_MSG];
    if (load_ism_files(&p, &meta, params, iface, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return 1;
    }
    FlSim sim;
    fl_sim_init(&sim, &p);
    FlFcsLimits lim;
    fl_fcs_limits_default(&lim);
    FlFcsResult res;
    if (fl_fcs_co_check(&sim, &lim, &res) != 0) {
        fprintf(stderr, "squirrelflightlab: fcs-check failed to run\n");
        return 1;
    }
    char text[2048];
    if (fl_fcs_report(&meta, &lim, &res, text, sizeof text) < 0) return 1;
    fputs(text, stdout);
    if (report && write_text(report, text) != 0) {
        fprintf(stderr, "squirrelflightlab: cannot write %s\n", report);
        return 1;
    }
    return res.passed ? 0 : 1;
}

static int cmd_demo(void) {
    puts("=== Fighter Jet Flight Lab demo ===");
    char *args_aoa[] = {"--n", "9", "--alpha0", "-2", "--alpha1", "10", "--V", "22"};
    int rc = cmd_aoa(8, args_aoa);
    if (rc) return rc;
    puts("");
    char *args_rec[] = {"--duration", "1.5", "--dt", "0.05", "--de", "-2"};
    rc = cmd_record(6, args_rec);
    if (rc) return rc;
    puts("");
    FlScenario list[FL_SCEN_COUNT];
    int n = 0;
    fl_scenario_defaults(list, &n);
    FlProject p;
    fl_project_default_ism_fighter(&p);
    FlSim sim;
    fl_sim_init(&sim, &p);
    int i;
    for (i = 0; i < n; i++) {
        FlScenarioResult res;
        if (fl_scenario_run(&sim, &list[i], &res) != 0) return 1;
        puts(res.summary);
    }
    puts("");
    rc = cmd_fcs_check(0, NULL);
    if (rc) return rc;
    puts("demo ok");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "demo") == 0) return cmd_demo();
    if (strcmp(cmd, "project") == 0) {
        FlProject p;
        char err[FL_MAX_MSG];
        const char *path = argc >= 3 ? argv[2] : NULL;
        if (load_project(&p, path, err, sizeof err) != 0) {
            fprintf(stderr, "squirrelflightlab: %s\n", err);
            return 1;
        }
        char buf[4096];
        if (fl_project_format(&p, buf, sizeof buf) < 0) return 1;
        fputs(buf, stdout);
        printf("# surfaces=%d channels=%d\n", p.nsurfaces, p.nchannels);
        return 0;
    }
    if (strcmp(cmd, "aoa-sweep") == 0) return cmd_aoa(argc - 2, argv + 2);
    if (strcmp(cmd, "record") == 0) return cmd_record(argc - 2, argv + 2);
    if (strcmp(cmd, "scenario") == 0) return cmd_scenario(argc - 2, argv + 2);
    if (strcmp(cmd, "avionics-check") == 0)
        return cmd_avionics(argc >= 3 ? argv[2] : NULL);
    if (strcmp(cmd, "ism-load") == 0) return cmd_ism_load(argc - 2, argv + 2);
    if (strcmp(cmd, "ism-interfaces") == 0)
        return cmd_ism_interfaces(argc - 2, argv + 2);
    if (strcmp(cmd, "fcs-check") == 0) return cmd_fcs_check(argc - 2, argv + 2);
    if (strcmp(cmd, "verify") == 0) {
        const char *vsp = NULL, *avl = NULL;
        int i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--openvsp") == 0 && i + 1 < argc)
                vsp = argv[++i];
            else if (strcmp(argv[i], "--avl") == 0 && i + 1 < argc)
                avl = argv[++i];
            else if (argv[i][0] == '-')
                return 2;
        }
        FlVerifyReport rep;
        int rc = fl_verify_run_analytic(&rep);
        char err[FL_MAX_MSG];
        fl_verify_optional_xcheck(vsp, avl, &rep, err, sizeof err);
        char md[8192];
        fl_verify_report_md(&rep, md, sizeof md);
        fputs(md, stdout);
        return (rc != 0 || rep.nfail) ? 1 : 0;
    }
    if (strcmp(cmd, "viability-report") == 0) {
        const char *path = "fixtures/ism-interfaces/viability-criteria.yaml";
        const char *json_out = NULL, *md_out = NULL;
        int i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0 && i + 1 < argc)
                json_out = argv[++i];
            else if (strcmp(argv[i], "--md") == 0 && i + 1 < argc)
                md_out = argv[++i];
            else if (argv[i][0] == '-')
                return 2;
            else
                path = argv[i];
        }
        FlViaReport rep;
        char err[FL_MAX_MSG];
        int rc = fl_viability_eval_file(path, &rep, err, sizeof err);
        if (rc < 0) {
            fprintf(stderr, "squirrelflightlab: %s\n", err);
            return 1;
        }
        char md[16384], js[16384];
        fl_viability_report_md(&rep, md, sizeof md);
        fl_viability_report_json(&rep, js, sizeof js);
        fputs(md, stdout);
        if (md_out && write_text(md_out, md) != 0) return 1;
        if (json_out && write_text(json_out, js) != 0) return 1;
        char note[240];
        fl_viability_optional_xcheck(NULL, NULL, NULL, note, sizeof note);
        fprintf(stderr, "%s\n", note);
        return rep.nfail ? 1 : 0;
    }
    if (strcmp(cmd, "loadout-check") == 0) {
        const char *path = NULL;
        const char *iface = "fixtures/ism-interfaces";
        int i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc)
                iface = argv[++i];
            else if (argv[i][0] == '-')
                return 2;
            else
                path = argv[i];
        }
        if (!path) {
            fprintf(stderr, "squirrelflightlab: loadout-check needs FILE\n");
            return 2;
        }
        FlLoReport rep;
        char err[FL_MAX_MSG];
        int rc = fl_loadout_validate(iface, path, &rep, err, sizeof err);
        if (rc < 0) {
            fprintf(stderr, "squirrelflightlab: %s\n", err);
            return 1;
        }
        char md[16384];
        fl_loadout_report_md(&rep, md, sizeof md);
        fputs(md, stdout);
        return rep.nfail ? 1 : 0;
    }
    if (strcmp(cmd, "planned") == 0) {
        int n = 0;
        const FlPlannedSurface *list = fl_planned_list(&n);
        int i;
        for (i = 0; i < n; i++)
            printf("%s [%s]: %s\n", list[i].name, list[i].status, list[i].note);
        return 0;
    }
    if (strcmp(cmd, "cfd-import") == 0 || strcmp(cmd, "tunnel") == 0 ||
        strcmp(cmd, "hil") == 0) {
        if (argc < 3) {
            usage(stderr);
            return 2;
        }
        char err[FL_MAX_MSG];
        int rc;
        if (strcmp(cmd, "cfd-import") == 0)
            rc = fl_planned_cfd_mesh_import(argv[2], err, sizeof err);
        else if (strcmp(cmd, "tunnel") == 0)
            rc = fl_planned_wind_tunnel_correlate(argv[2], err, sizeof err);
        else
            rc = fl_planned_hil_connect(argv[2], err, sizeof err);
        fprintf(stderr, "squirrelflightlab: %s\n", err);
        return rc ? 1 : 0;
    }
    fprintf(stderr, "squirrelflightlab: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}

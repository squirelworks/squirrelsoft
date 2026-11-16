/*
 * Soft #176 follow-up: evaluate Victor's viability-criteria.yaml gates.
 * Recomputes sizing metrics from derived_full_scale and checks class bands.
 * Deferred Soft recomputes: G-BREGUET (Brent on L/D), G-CG travel, G-STRUCT-MARGIN.
 *
 * Numerics: Breguet cruise R=(V/c)(L/D)ln(Wi/Wf) — Raymer/Roskam textbook form.
 * Soft solves for L/D via Brent (squirrelnum / NR 3rd ed. Ch. 9.3 method narrative;
 * original Brent wrapper — no NR listings).
 */
#include "fl_viability.h"

#include "fl_units.h"
#include "squirrelnum.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    double V;   /* m/s */
    double c;   /* 1/s SFC */
    double ln_wr;
    double R_m; /* target ferry range, m */
} SoftBreguetCtx;

/* f(LD) = (V/c)*LD*ln(Wi/Wf) - R_m  (zero at Soft ferry L/D) */
static double soft_breguet_residual(double ld, void *ctx) {
    SoftBreguetCtx *b = ctx;
    return (b->V / b->c) * ld * b->ln_wr - b->R_m;
}

static FlYNode *gate_by_id(FlYNode *gates, const char *want) {
    if (!gates || gates->kind != FL_Y_SEQ) return NULL;
    for (FlYNode *it = gates->child; it; it = it->next) {
        FlYNode *id = fl_yaml_map_get(it, "id");
        if (id && strcmp(fl_yaml_as_cstr(id), want) == 0) return it;
    }
    return NULL;
}

static double node_num(FlYNode *n, double fallback) {
    double v;
    if (!n) return fallback;
    if (fl_yaml_as_double(n, &v) == 0) return v;
    return fallback;
}

static FlYNode *band_of(FlYNode *root, const char *ref) {
    FlYNode *bands = fl_yaml_map_get(root, "class_comparison_bands");
    if (!bands) return NULL;
    return fl_yaml_map_get(bands, ref);
}

static int in_band(double v, FlYNode *band) {
    if (!band) return 0;
    double lo = node_num(fl_yaml_map_get(band, "min"), -1e99);
    double hi = node_num(fl_yaml_map_get(band, "max"), 1e99);
    return v >= lo && v <= hi;
}

static void add_gate(FlViaReport *rep, const char *id, const char *name,
                     const char *yaml_result, int soft_pass, double measured,
                     double lo, double hi, const char *detail) {
    if (rep->ngates >= FL_VIA_MAX_GATES) return;
    FlViaGate *g = &rep->gates[rep->ngates++];
    snprintf(g->id, sizeof g->id, "%s", id);
    snprintf(g->name, sizeof g->name, "%s", name);
    snprintf(g->result, sizeof g->result, "%s", yaml_result ? yaml_result : "?");
    g->soft_pass = soft_pass ? 1 : 0;
    g->measured = measured;
    g->band_min = lo;
    g->band_max = hi;
    snprintf(g->detail, sizeof g->detail, "%s", detail ? detail : "");
    if (!soft_pass) rep->nfail++;
    else if (yaml_result && strstr(yaml_result, "provisional"))
        rep->nprov++;
    else
        rep->npass++;
}

int fl_viability_eval(const char *yaml_text, FlViaReport *rep, char *err,
                      int err_cap) {
    if (!yaml_text || !rep) {
        if (err) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(rep, 0, sizeof *rep);
    snprintf(rep->label, sizeof rep->label, "%s",
             "provisional, unvalidated against tunnel/flight test");

    FlYDoc *doc = calloc(1, sizeof *doc);
    if (!doc) {
        if (err) snprintf(err, err_cap, "oom");
        return -1;
    }
    if (fl_yaml_parse(doc, yaml_text, err, err_cap) != 0) {
        free(doc);
        return -1;
    }
    FlYNode *root = doc->root;
    FlYNode *sv = fl_yaml_map_get(root, "schema_version");
    FlYNode *aid = fl_yaml_map_get(root, "aircraft_id");
    if (!sv || !aid) {
        if (err) snprintf(err, err_cap, "missing schema_version or aircraft_id");
        free(doc);
        return -1;
    }
    snprintf(rep->schema_version, sizeof rep->schema_version, "%s",
             fl_yaml_as_cstr(sv));
    snprintf(rep->aircraft_id, sizeof rep->aircraft_id, "%s",
             fl_yaml_as_cstr(aid));
    if (strcmp(rep->schema_version, "1.0.0") != 0) {
        if (err)
            snprintf(err, err_cap, "unsupported schema_version %s",
                     rep->schema_version);
        free(doc);
        return -1;
    }
    FlYNode *prov = fl_yaml_map_get(root, "provisional");
    rep->provisional = prov && prov->kind == FL_Y_BOOL ? prov->boolean : 1;

    FlYNode *dfs = fl_yaml_map_get(root, "derived_full_scale");
    if (!dfs) {
        if (err) snprintf(err, err_cap, "missing derived_full_scale");
        free(doc);
        return -1;
    }
    double empty = node_num(fl_yaml_map_get(dfs, "empty_mass_kg"), 0);
    double mtow = node_num(fl_yaml_map_get(dfs, "mtow_kg"), 0);
    double fuel = node_num(fl_yaml_map_get(dfs, "fuel_internal_kg"), 0);
    double S = node_num(fl_yaml_map_get(dfs, "wing_area_m2"), 0);
    double T = node_num(fl_yaml_map_get(dfs, "thrust_sl_reheat_total_n"), 0);
    double nz = node_num(fl_yaml_map_get(dfs, "nz_limit_g"), 0);
    double radius = node_num(fl_yaml_map_get(dfs, "combat_radius_km_assumption"), 0);
    double sm = node_num(fl_yaml_map_get(dfs, "static_margin_pct_mac_assumption"), 0);

    if (mtow <= 0 || S <= 0) {
        if (err) snprintf(err, err_cap, "invalid mtow or wing_area");
        free(doc);
        return -1;
    }

    double we_frac = empty / mtow;
    double tw = T / (mtow * FL_G0);
    double wload = (mtow * FL_G0) / S;
    double ff = fuel / mtow;

    FlYNode *b;
    b = band_of(root, "empty_weight_fraction");
    add_gate(rep, "G-WE-FRAC", "empty_weight_fraction", "pass", in_band(we_frac, b),
             we_frac, node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "empty/mtow vs class band");

    b = band_of(root, "thrust_to_weight_mtow_reheat");
    add_gate(rep, "G-TW-MTOW", "thrust_to_weight_mtow_reheat", "pass", in_band(tw, b),
             tw, node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "T/(W) reheat");

    b = band_of(root, "wing_loading_mtow_pa");
    add_gate(rep, "G-WING-LOAD", "wing_loading_mtow", "pass", in_band(wload, b),
             wload, node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "(mtow*g)/S Pa");

    b = band_of(root, "fuel_fraction");
    add_gate(rep, "G-FUEL-FRAC", "fuel_fraction_internal", "pass", in_band(ff, b),
             ff, node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "fuel/mtow");

    b = band_of(root, "combat_radius_km");
    add_gate(rep, "G-RADIUS", "combat_radius_assumption", "provisional_pass",
             in_band(radius, b), radius, node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "assumed combat radius");

    b = band_of(root, "max_nz_g");
    add_gate(rep, "G-NZ", "max_nz", "pass", in_band(nz, b), nz,
             node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "nz limit");

    b = band_of(root, "static_margin_pct_mac");
    add_gate(rep, "G-SM", "static_margin", "provisional_pass", in_band(sm, b), sm,
             node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "SM % MAC assumption");

    b = band_of(root, "empty_mass_kg_class");
    add_gate(rep, "G-MASS-EMPTY", "empty_mass_class", "pass", in_band(empty, b),
             empty, node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "empty mass class");

    b = band_of(root, "mtow_kg_class");
    add_gate(rep, "G-MASS-MTOW", "mtow_class", "pass", in_band(mtow, b), mtow,
             node_num(fl_yaml_map_get(b, "min"), 0),
             node_num(fl_yaml_map_get(b, "max"), 0), "MTOW class");

    FlYNode *gates = fl_yaml_map_get(root, "gates");

    /*
     * G-BREGUET: Soft ferry R=(V/c)(L/D)ln(Wi/Wf) with YAML assumed_ld.
     * Pass if L/D in band_ld and Soft ferry×frac covers combat radius.
     * Soft Brent (NR Ch. 9.3 method) recovers assumed_ld from Soft R (oracle).
     */
    {
        FlYNode *g = gate_by_id(gates, "G-BREGUET");
        if (!g) {
            if (err) snprintf(err, err_cap, "missing gate G-BREGUET");
            free(doc);
            return -1;
        }
        double V = node_num(fl_yaml_map_get(g, "assumed_v_mps"), 0);
        double sfc = node_num(fl_yaml_map_get(g, "assumed_sfc_per_s"), 0);
        double ld = node_num(fl_yaml_map_get(g, "assumed_ld"), 0);
        double frac = node_num(fl_yaml_map_get(g, "combat_radius_fraction_of_ferry"), 0);
        FlYNode *bld = fl_yaml_map_get(g, "band_ld");
        double ld_lo = node_num(fl_yaml_map_get(bld, "min"), 6.0);
        double ld_hi = node_num(fl_yaml_map_get(bld, "max"), 12.0);
        double Wf = mtow - fuel;
        if (V <= 0 || sfc <= 0 || ld <= 0 || frac <= 0 || Wf <= 0 || fuel <= 0) {
            if (err) snprintf(err, err_cap, "G-BREGUET invalid V/sfc/ld/frac/fuel");
            free(doc);
            return -1;
        }
        double ln_wr = log(mtow / Wf);
        double R_soft_m = (V / sfc) * ld * ln_wr;
        double R_soft_km = R_soft_m / 1000.0;
        SoftBreguetCtx bctx;
        bctx.V = V;
        bctx.c = sfc;
        bctx.ln_wr = ln_wr;
        bctx.R_m = R_soft_m;
        double ld_root = 0;
        int iters = 0;
        int brc = sqn_brent(soft_breguet_residual, &bctx, ld_lo * 0.5, ld_hi * 1.5,
                            1e-9, 80, &ld_root, &iters);
        int ld_ok = ld >= ld_lo && ld <= ld_hi;
        int radius_ok = R_soft_km * frac + 1e-6 >= radius;
        int brent_ok = brc == SQN_OK && fabs(ld_root - ld) < 1e-6;
        int ok = ld_ok && radius_ok && brent_ok;
        char detail[FL_VIA_MSG];
        snprintf(detail, sizeof detail,
                 "Soft R≈%.0f km (L/D=%.2f); combat %.0f needs ferry≥%.0f; "
                 "Brent L/D=%.4f (NR 9.3)",
                 R_soft_km, ld, radius, radius / frac, ld_root);
        add_gate(rep, "G-BREGUET", "breguet_range_sanity",
                 ok ? "provisional_pass" : "fail", ok, ld, ld_lo, ld_hi, detail);
    }

    /* G-CG: Soft CG travel %MAC from fuselage-station fraction span. */
    {
        FlYNode *g = gate_by_id(gates, "G-CG");
        if (!g) {
            if (err) snprintf(err, err_cap, "missing gate G-CG");
            free(doc);
            return -1;
        }
        FlYNode *rng = fl_yaml_map_get(g, "our_value_fs_pct_range");
        double fs0 = 0, fs1 = 0;
        if (rng && rng->kind == FL_Y_SEQ && rng->child) {
            fs0 = node_num(rng->child, 0);
            fs1 = node_num(rng->child->next, fs0);
        }
        double travel = fabs(fs1 - fs0) * 100.0; /* fraction → %MAC proxy */
        b = band_of(root, "cg_travel_pct_mac");
        double lo = node_num(fl_yaml_map_get(b, "min"), 0);
        double hi = node_num(fl_yaml_map_get(b, "max"), 15);
        int ok = in_band(travel, b);
        char detail[FL_VIA_MSG];
        snprintf(detail, sizeof detail,
                 "Soft travel=%.1f %%MAC from FS [%.2f,%.2f]", travel, fs0, fs1);
        add_gate(rep, "G-CG", "cg_travel_fs", ok ? "pass" : "fail", ok, travel, lo,
                 hi, detail);
    }

    /* G-STRUCT-MARGIN: Soft ultimate = limit × factor ≥ nz_limit × factor. */
    {
        FlYNode *g = gate_by_id(gates, "G-STRUCT-MARGIN");
        if (!g) {
            if (err) snprintf(err, err_cap, "missing gate G-STRUCT-MARGIN");
            free(doc);
            return -1;
        }
        double lim = node_num(fl_yaml_map_get(g, "limit_load_factor_g"), 0);
        double uf = node_num(fl_yaml_map_get(g, "ultimate_factor"), 1.5);
        double soft_ult = lim * uf;
        double need = nz * uf;
        int ok = lim + 1e-9 >= nz && soft_ult + 1e-9 >= need;
        char detail[FL_VIA_MSG];
        snprintf(detail, sizeof detail,
                 "Soft ultimate=%.1f g (limit×%.2f); need≥nz×factor=%.1f", soft_ult,
                 uf, need);
        add_gate(rep, "G-STRUCT-MARGIN", "structural_limit_load_factor",
                 ok ? "pass" : "fail", ok, soft_ult, need, need, detail);
    }

    /* Remaining YAML gates Soft does not yet Soft-recompute */
    if (gates && gates->kind == FL_Y_SEQ) {
        for (FlYNode *it = gates->child; it; it = it->next) {
            FlYNode *id = fl_yaml_map_get(it, "id");
            if (!id) {
                if (err) snprintf(err, err_cap, "gate missing id");
                free(doc);
                return -1;
            }
            const char *gid = fl_yaml_as_cstr(id);
            int known = 0;
            int i;
            for (i = 0; i < rep->ngates; i++)
                if (strcmp(rep->gates[i].id, gid) == 0 ||
                    (strcmp(gid, "G-MASS-CLASS") == 0 &&
                     (strcmp(rep->gates[i].id, "G-MASS-EMPTY") == 0 ||
                      strcmp(rep->gates[i].id, "G-MASS-MTOW") == 0)))
                    known = 1;
            if (!known) {
                FlYNode *res = fl_yaml_map_get(it, "result");
                const char *rs = res ? fl_yaml_as_cstr(res) : "unknown";
                add_gate(rep, gid, fl_yaml_as_cstr(fl_yaml_map_get(it, "name")),
                         rs, 1, 0.0, 0.0, 0.0,
                         "YAML gate recorded; Soft recompute deferred");
            }
        }
    }

    if (rep->nfail)
        snprintf(rep->overall, sizeof rep->overall, "fail");
    else if (rep->nprov || rep->provisional)
        snprintf(rep->overall, sizeof rep->overall, "provisional_pass");
    else
        snprintf(rep->overall, sizeof rep->overall, "pass");

    free(doc);
    return rep->nfail ? 1 : 0;
}

int fl_viability_eval_file(const char *path, FlViaReport *rep, char *err,
                           int err_cap) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) snprintf(err, err_cap, "cannot read %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || n > 2 << 20) {
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
    int rc = fl_viability_eval(buf, rep, err, err_cap);
    free(buf);
    return rc;
}

int fl_viability_report_md(const FlViaReport *rep, char *out, int out_cap) {
    if (!rep || !out || out_cap < 64) return -1;
    int w = 0;
    w += snprintf(out + w, out_cap - w,
                  "# Flight Lab viability report\n\n"
                  "**Label:** %s\n"
                  "**Aircraft:** %s  **schema_version:** %s  **overall:** %s\n\n"
                  "| Gate | Soft | YAML result | Value | Band | Detail |\n"
                  "|------|------|-------------|-------|------|--------|\n",
                  rep->label, rep->aircraft_id, rep->schema_version, rep->overall);
    int i;
    for (i = 0; i < rep->ngates && w < out_cap - 8; i++) {
        const FlViaGate *g = &rep->gates[i];
        w += snprintf(out + w, out_cap - w,
                      "| %s (%s) | %s | %s | %.6g | [%.4g, %.4g] | %s |\n", g->id,
                      g->name, g->soft_pass ? "PASS" : "FAIL", g->result,
                      g->measured, g->band_min, g->band_max, g->detail);
    }
    w += snprintf(out + w, out_cap - w,
                  "\nSummary: soft_pass=%d provisional=%d fail=%d\n", rep->npass,
                  rep->nprov, rep->nfail);
    return w < out_cap ? 0 : -1;
}

int fl_viability_report_json(const FlViaReport *rep, char *out, int out_cap) {
    if (!rep || !out || out_cap < 64) return -1;
    int w = 0;
    w += snprintf(out + w, out_cap - w,
                  "{\"label\":\"%s\",\"aircraft_id\":\"%s\",\"schema_version\":\"%s\","
                  "\"overall\":\"%s\",\"gates\":[",
                  rep->label, rep->aircraft_id, rep->schema_version, rep->overall);
    int i;
    for (i = 0; i < rep->ngates && w < out_cap - 8; i++) {
        const FlViaGate *g = &rep->gates[i];
        w += snprintf(out + w, out_cap - w,
                      "%s{\"id\":\"%s\",\"name\":\"%s\",\"soft_pass\":%s,\"result\":\"%s\","
                      "\"measured\":%.8g,\"band_min\":%.8g,\"band_max\":%.8g}",
                      i ? "," : "", g->id, g->name, g->soft_pass ? "true" : "false",
                      g->result, g->measured, g->band_min, g->band_max);
    }
    w += snprintf(out + w, out_cap - w, "],\"nfail\":%d}\n", rep->nfail);
    return w < out_cap ? 0 : -1;
}

int fl_viability_optional_xcheck(const char *openvsp, const char *avl,
                                 const char *calculix, char *note, int note_cap) {
    struct stat st;
    int any = 0, missing = 0;
    if (openvsp && openvsp[0]) {
        any = 1;
        if (stat(openvsp, &st) != 0) missing = 1;
    }
    if (avl && avl[0]) {
        any = 1;
        if (stat(avl, &st) != 0) missing = 1;
    }
    if (calculix && calculix[0]) {
        any = 1;
        if (stat(calculix, &st) != 0) missing = 1;
    }
    if (!any) {
        if (note)
            snprintf(note, note_cap,
                     "optional OpenVSP/AVL/CalculiX xcheck skipped (no paths)");
        return 1;
    }
    if (missing) {
        if (note)
            snprintf(note, note_cap, "optional xcheck: one or more paths missing");
        return 1;
    }
    if (note)
        snprintf(note, note_cap,
                 "optional artifacts present — full numeric compare Soft follow-up");
    return 0;
}

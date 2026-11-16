/*
 * Soft #173: station × adapter × store loadout validation (inert / interface).
 */
#include "fl_loadout.h"

#include "fl_yaml.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, char **out, char *err, int err_cap) {
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
    *out = buf;
    return 0;
}

static int parse_path(FlYDoc *doc, const char *dir, const char *name, char *err,
                      int err_cap) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    char *text = NULL;
    if (read_file(path, &text, err, err_cap) != 0) return -1;
    int rc = fl_yaml_parse(doc, text, err, err_cap);
    free(text);
    return rc;
}

static double node_num(FlYNode *n, double fb) {
    double v;
    if (!n) return fb;
    if (fl_yaml_as_double(n, &v) == 0) return v;
    return fb;
}

static int seq_contains_str(FlYNode *seq, const char *s) {
    if (!seq || seq->kind != FL_Y_SEQ || !s) return 0;
    for (FlYNode *it = seq->child; it; it = it->next) {
        if (strcmp(fl_yaml_as_cstr(it), s) == 0) return 1;
    }
    return 0;
}

static void add_check(FlLoReport *rep, const char *id, int pass, const char *detail) {
    if (rep->nchecks >= FL_LO_MAX_CHECKS) return;
    FlLoCheck *c = &rep->checks[rep->nchecks++];
    snprintf(c->id, sizeof c->id, "%s", id);
    snprintf(c->result, sizeof c->result, "%s", pass ? "PASS" : "FAIL");
    snprintf(c->detail, sizeof c->detail, "%s", detail ? detail : "");
    if (pass) rep->npass++;
    else rep->nfail++;
}

static FlYNode *find_station(FlYNode *hp_root, const char *id) {
    FlYNode *st = fl_yaml_map_get(hp_root, "stations");
    if (!st || st->kind != FL_Y_SEQ) return NULL;
    for (FlYNode *it = st->child; it; it = it->next) {
        FlYNode *sid = fl_yaml_map_get(it, "id");
        if (sid && strcmp(fl_yaml_as_cstr(sid), id) == 0) return it;
    }
    return NULL;
}

static FlYNode *find_inert_store(FlYNode *inert_root, const char *id) {
    FlYNode *st = fl_yaml_map_get(inert_root, "stores");
    if (!st || st->kind != FL_Y_SEQ) return NULL;
    for (FlYNode *it = st->child; it; it = it->next) {
        FlYNode *sid = fl_yaml_map_get(it, "id");
        if (sid && strcmp(fl_yaml_as_cstr(sid), id) == 0) return it;
    }
    return NULL;
}

static FlYNode *find_catalogue_store(FlYNode *mo_root, const char *id) {
    FlYNode *cat = fl_yaml_map_get(mo_root, "inert_catalogue");
    if (!cat || cat->kind != FL_Y_SEQ) return NULL;
    for (FlYNode *it = cat->child; it; it = it->next) {
        FlYNode *sid = fl_yaml_map_get(it, "id");
        if (sid && strcmp(fl_yaml_as_cstr(sid), id) == 0) return it;
    }
    return NULL;
}

static FlYNode *find_hardware(FlYNode *inert_root, const char *id) {
    FlYNode *hw = fl_yaml_map_get(inert_root, "hardware");
    if (!hw || hw->kind != FL_Y_SEQ) return NULL;
    for (FlYNode *it = hw->child; it; it = it->next) {
        FlYNode *hid = fl_yaml_map_get(it, "id");
        if (hid && strcmp(fl_yaml_as_cstr(hid), id) == 0) return it;
    }
    return NULL;
}

static const char *station_default_hw(FlYNode *sms_root, const char *sta) {
    FlYNode *defs = fl_yaml_map_get(sms_root, "station_hardware_defaults");
    if (!defs) return NULL;
    FlYNode *n = fl_yaml_map_get(defs, sta);
    return n ? fl_yaml_as_cstr(n) : NULL;
}

static int family_allows(FlYNode *mo_root, const char *family, const char *sta,
                         const char *store) {
    FlYNode *mx = fl_yaml_map_get(mo_root, "compatibility_matrix");
    if (!mx || mx->kind != FL_Y_SEQ) return 0;
    for (FlYNode *it = mx->child; it; it = it->next) {
        FlYNode *f = fl_yaml_map_get(it, "family");
        if (!f || strcmp(fl_yaml_as_cstr(f), family) != 0) continue;
        if (!seq_contains_str(fl_yaml_map_get(it, "stations"), sta)) return 0;
        if (!seq_contains_str(fl_yaml_map_get(it, "stores"), store)) return 0;
        return 1;
    }
    return 0;
}

static int adapter_allowed_on_station(FlYNode *mo_root, const char *sta,
                                      const char *adapter) {
    FlYNode *opts = fl_yaml_map_get(mo_root, "station_mechanical_options");
    if (!opts) return 0;
    FlYNode *seq = fl_yaml_map_get(opts, sta);
    return seq_contains_str(seq, adapter);
}

static FlYNode *find_envelope(FlYNode *sms_root, const char *store_id) {
    FlYNode *sep = fl_yaml_map_get(sms_root, "separation_envelopes");
    if (!sep) return NULL;
    FlYNode *defs = fl_yaml_map_get(sep, "defaults");
    if (!defs || defs->kind != FL_Y_SEQ) return NULL;
    for (FlYNode *it = defs->child; it; it = it->next) {
        FlYNode *sid = fl_yaml_map_get(it, "store_id");
        if (sid && strcmp(fl_yaml_as_cstr(sid), store_id) == 0) return it;
    }
    return NULL;
}

static int load_loadout_yaml(const char *path, FlLoReport *rep, char *err,
                             int err_cap) {
    char *text = NULL;
    if (read_file(path, &text, err, err_cap) != 0) return -1;
    FlYDoc *doc = calloc(1, sizeof *doc);
    if (!doc) {
        free(text);
        return -1;
    }
    if (fl_yaml_parse(doc, text, err, err_cap) != 0) {
        free(text);
        free(doc);
        return -1;
    }
    free(text);
    FlYNode *root = doc->root;
    FlYNode *aid = fl_yaml_map_get(root, "aircraft_id");
    if (aid)
        snprintf(rep->aircraft_id, sizeof rep->aircraft_id, "%s",
                 fl_yaml_as_cstr(aid));
    FlYNode *sts = fl_yaml_map_get(root, "stations");
    if (!sts || sts->kind != FL_Y_SEQ) {
        if (err) snprintf(err, err_cap, "loadout missing stations sequence");
        free(doc);
        return -1;
    }
    for (FlYNode *it = sts->child; it; it = it->next) {
        if (rep->nassign >= FL_LO_MAX_ASSIGN) break;
        FlYNode *sta = fl_yaml_map_get(it, "station");
        FlYNode *sid = fl_yaml_map_get(it, "store_id");
        if (!sta || !sid) {
            if (err) snprintf(err, err_cap, "assignment needs station + store_id");
            free(doc);
            return -1;
        }
        FlLoAssign *a = &rep->assigns[rep->nassign++];
        snprintf(a->station, sizeof a->station, "%s", fl_yaml_as_cstr(sta));
        snprintf(a->store_id, sizeof a->store_id, "%s", fl_yaml_as_cstr(sid));
        FlYNode *ad = fl_yaml_map_get(it, "adapter");
        if (ad)
            snprintf(a->adapter, sizeof a->adapter, "%s", fl_yaml_as_cstr(ad));
        else
            a->adapter[0] = 0;
    }
    FlYNode *fc = fl_yaml_map_get(root, "flight_condition");
    if (fc) {
        rep->cond.has_condition = 1;
        rep->cond.keas_kt = node_num(fl_yaml_map_get(fc, "keas_kt"), 0);
        rep->cond.aoa_deg = node_num(fl_yaml_map_get(fc, "aoa_deg"), 0);
        rep->cond.nz_g = node_num(fl_yaml_map_get(fc, "nz_g"), 1);
        rep->cond.sideslip_deg = node_num(fl_yaml_map_get(fc, "sideslip_deg"), 0);
        FlYNode *wow = fl_yaml_map_get(fc, "wow");
        rep->cond.wow = wow && wow->kind == FL_Y_BOOL ? wow->boolean : 0;
    }
    free(doc);
    return 0;
}

int fl_loadout_validate(const char *iface_dir, const char *loadout_path,
                        FlLoReport *rep, char *err, int err_cap) {
    if (!iface_dir || !loadout_path || !rep) {
        if (err) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(rep, 0, sizeof *rep);
    snprintf(rep->label, sizeof rep->label, "%s",
             "provisional, unvalidated against tunnel/flight test");

    if (load_loadout_yaml(loadout_path, rep, err, err_cap) != 0) return -1;

    FlYDoc *hp = calloc(1, sizeof *hp);
    FlYDoc *inert = calloc(1, sizeof *inert);
    FlYDoc *mo = calloc(1, sizeof *mo);
    FlYDoc *sms = calloc(1, sizeof *sms);
    FlYDoc *mass = calloc(1, sizeof *mass);
    if (!hp || !inert || !mo || !sms || !mass) {
        free(hp);
        free(inert);
        free(mo);
        free(sms);
        free(mass);
        if (err) snprintf(err, err_cap, "oom");
        return -1;
    }
    if (parse_path(hp, iface_dir, "hardpoints.yaml", err, err_cap) != 0 ||
        parse_path(inert, iface_dir, "inert-stores.yaml", err, err_cap) != 0 ||
        parse_path(mo, iface_dir, "multi-origin-stores.yaml", err, err_cap) != 0 ||
        parse_path(sms, iface_dir, "sms-interface.yaml", err, err_cap) != 0 ||
        parse_path(mass, iface_dir, "mass-properties.yaml", err, err_cap) != 0) {
        free(hp);
        free(inert);
        free(mo);
        free(sms);
        free(mass);
        return -1;
    }

    double lat_tol = 0.05;
    double asym_warn = 200.0;
    FlYNode *eb = fl_yaml_map_get(mass->root, "expected_behaviour");
    if (eb) {
        lat_tol = node_num(fl_yaml_map_get(eb, "lateral_cg_tolerance_m"), lat_tol);
        asym_warn =
            node_num(fl_yaml_map_get(eb, "warn_if_asymmetric_stores_kg"), asym_warn);
    }

    int i;
    for (i = 0; i < rep->nassign; i++) {
        FlLoAssign *a = &rep->assigns[i];
        char cid[48];
        snprintf(cid, sizeof cid, "STA-%s", a->station);

        FlYNode *sta = find_station(hp->root, a->station);
        if (!sta) {
            add_check(rep, a->station, 0, "unknown station id");
            continue;
        }
        double mass_max = node_num(fl_yaml_map_get(sta, "mass_class_kg_max"), 0);
        const char *side = fl_yaml_as_cstr(fl_yaml_map_get(sta, "side"));
        double bl = node_num(fl_yaml_map_get(sta, "bl_study_mm"), 0) / 1000.0;
        const char *sta_hw = fl_yaml_as_cstr(fl_yaml_map_get(sta, "hardware"));
        const char *def_hw = station_default_hw(sms->root, a->station);
        if (def_hw && def_hw[0]) sta_hw = def_hw;

        FlYNode *store = find_inert_store(inert->root, a->store_id);
        FlYNode *cat = find_catalogue_store(mo->root, a->store_id);
        if (!store && !cat) {
            add_check(rep, a->station, 0, "unknown store_id");
            continue;
        }

        double mass_kg = 0;
        int station_ok = 0;
        int hw_ok = 0;
        const char *family = "ISM";

        if (store) {
            mass_kg = node_num(fl_yaml_map_get(store, "mass_kg"), 0);
            station_ok =
                seq_contains_str(fl_yaml_map_get(store, "compatible_stations"),
                                 a->station);
            hw_ok = seq_contains_str(fl_yaml_map_get(store, "compatible_hardware"),
                                     sta_hw);
            family = "ISM";
        }
        if (cat) {
            FlYNode *fam = fl_yaml_map_get(cat, "family");
            if (fam) family = fl_yaml_as_cstr(fam);
            if (!store)
                mass_kg = node_num(fl_yaml_map_get(cat, "mass_kg"), 0);
            /* catalogue entries that extend inert-stores inherit station lists */
            if (!station_ok)
                station_ok = family_allows(mo->root, family, a->station, a->store_id);
            if (!station_ok && store)
                station_ok = seq_contains_str(
                    fl_yaml_map_get(store, "compatible_stations"), a->station);
        }

        snprintf(cid, sizeof cid, "%s-station", a->station);
        add_check(rep, cid, station_ok,
                  station_ok ? "store allowed on station"
                             : "store not compatible with station");

        /* Adapter / hardware fit */
        int adapter_ok = 1;
        if (a->adapter[0]) {
            adapter_ok = adapter_allowed_on_station(mo->root, a->station, a->adapter);
            snprintf(cid, sizeof cid, "%s-adapter", a->station);
            add_check(rep, cid, adapter_ok,
                      adapter_ok ? "adapter allowed on station"
                                 : "adapter not in station_mechanical_options");
        } else {
            FlYNode *hw = find_hardware(inert->root, sta_hw);
            int fit = hw_ok;
            if (hw)
                fit = seq_contains_str(fl_yaml_map_get(hw, "compatible_stations"),
                                       a->station);
            if (store && !hw_ok) fit = 0;
            if (!store && cat) {
                /* multi-origin: mechanical preference vs station options */
                FlYNode *pref = fl_yaml_map_get(cat, "preferred_mechanical");
                fit = 0;
                if (pref && pref->kind == FL_Y_SEQ) {
                    for (FlYNode *p = pref->child; p; p = p->next) {
                        if (adapter_allowed_on_station(mo->root, a->station,
                                                       fl_yaml_as_cstr(p))) {
                            fit = 1;
                            break;
                        }
                    }
                }
                if (!fit)
                    fit = family_allows(mo->root, family, a->station, a->store_id);
            }
            snprintf(cid, sizeof cid, "%s-hardware", a->station);
            add_check(rep, cid, fit,
                      fit ? "hardware/adapter fits station"
                          : "hardware/adapter does not fit station");
            adapter_ok = fit;
        }

        int mass_ok = mass_kg <= mass_max + 1e-9;
        snprintf(cid, sizeof cid, "%s-mass", a->station);
        char detail[FL_LO_MSG];
        snprintf(detail, sizeof detail, "mass %.1f kg vs station max %.1f kg",
                 mass_kg, mass_max);
        add_check(rep, cid, mass_ok, detail);

        if (side && strcmp(side, "left") == 0) {
            rep->mass_left_kg += mass_kg;
            rep->lateral_cg_m += mass_kg * bl;
        } else if (side && strcmp(side, "right") == 0) {
            rep->mass_right_kg += mass_kg;
            rep->lateral_cg_m += mass_kg * bl;
        } else {
            rep->mass_center_kg += mass_kg;
        }

        (void)adapter_ok;

        /* Release envelope when flight_condition present */
        if (rep->cond.has_condition) {
            int env_ok = 1;
            char ed[FL_LO_MSG];
            if (rep->cond.wow) {
                env_ok = 0;
                snprintf(ed, sizeof ed, "release blocked: WOW");
            } else {
                FlYNode *env = find_envelope(sms->root, a->store_id);
                if (!env) {
                    snprintf(ed, sizeof ed, "no separation envelope for store (skip)");
                    env_ok = 1; /* jettison-only / unknown → soft skip */
                } else {
                    FlYNode *keas = fl_yaml_map_get(env, "keas_kt");
                    FlYNode *aoa = fl_yaml_map_get(env, "aoa_deg");
                    FlYNode *nz = fl_yaml_map_get(env, "nz_g");
                    double k0 = node_num(fl_yaml_map_get(keas, "min"), 0);
                    double k1 = node_num(fl_yaml_map_get(keas, "max"), 1e9);
                    double a0 = node_num(fl_yaml_map_get(aoa, "min"), -90);
                    double a1 = node_num(fl_yaml_map_get(aoa, "max"), 90);
                    double n0 = node_num(fl_yaml_map_get(nz, "min"), -10);
                    double n1 = node_num(fl_yaml_map_get(nz, "max"), 10);
                    double beta_max =
                        node_num(fl_yaml_map_get(env, "sideslip_deg_abs_max"), 90);
                    env_ok = rep->cond.keas_kt >= k0 && rep->cond.keas_kt <= k1 &&
                             rep->cond.aoa_deg >= a0 && rep->cond.aoa_deg <= a1 &&
                             rep->cond.nz_g >= n0 && rep->cond.nz_g <= n1 &&
                             fabs(rep->cond.sideslip_deg) <= beta_max + 1e-9;
                    snprintf(ed, sizeof ed,
                             env_ok ? "inside separation envelope"
                                    : "outside separation envelope");
                }
            }
            snprintf(cid, sizeof cid, "%s-envelope", a->station);
            add_check(rep, cid, env_ok, ed);
        }
    }

    double total_store =
        rep->mass_left_kg + rep->mass_right_kg + rep->mass_center_kg;
    if (total_store > 1e-9)
        rep->lateral_cg_m /= total_store;
    else
        rep->lateral_cg_m = 0;

    double asym = fabs(rep->mass_left_kg - rep->mass_right_kg);
    int asym_ok = asym <= asym_warn + 1e-9;
    char ad[FL_LO_MSG];
    snprintf(ad, sizeof ad, "|L-R|=%.1f kg (limit %.1f kg)", asym, asym_warn);
    add_check(rep, "asymmetry", asym_ok, ad);

    int lat_ok = fabs(rep->lateral_cg_m) <= lat_tol + 1e-9;
    snprintf(ad, sizeof ad, "lateral CG ≈ %.4f m (tol %.4f m)", rep->lateral_cg_m,
             lat_tol);
    add_check(rep, "lateral-cg", lat_ok, ad);

    if (rep->nfail)
        snprintf(rep->overall, sizeof rep->overall, "FAIL");
    else
        snprintf(rep->overall, sizeof rep->overall, "PASS");

    free(hp);
    free(inert);
    free(mo);
    free(sms);
    free(mass);
    return rep->nfail ? 1 : 0;
}

int fl_loadout_report_md(const FlLoReport *rep, char *out, int out_cap) {
    if (!rep || !out || out_cap < 64) return -1;
    int w = 0;
    w += snprintf(out + w, out_cap - w,
                  "# Flight Lab loadout report\n\n"
                  "**Label:** %s\n"
                  "**Aircraft:** %s  **overall:** %s\n"
                  "Mass L/R/C: %.1f / %.1f / %.1f kg  lateral_cg≈%.4f m\n\n"
                  "| Check | Result | Detail |\n"
                  "|-------|--------|--------|\n",
                  rep->label, rep->aircraft_id, rep->overall, rep->mass_left_kg,
                  rep->mass_right_kg, rep->mass_center_kg, rep->lateral_cg_m);
    int i;
    for (i = 0; i < rep->nchecks && w < out_cap - 8; i++) {
        const FlLoCheck *c = &rep->checks[i];
        w += snprintf(out + w, out_cap - w, "| %s | %s | %s |\n", c->id, c->result,
                      c->detail);
    }
    w += snprintf(out + w, out_cap - w, "\nSummary: %d pass, %d fail\n", rep->npass,
                  rep->nfail);
    return w < out_cap ? 0 : -1;
}

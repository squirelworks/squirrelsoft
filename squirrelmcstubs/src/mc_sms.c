#include "mc_sms.h"

#include <stdio.h>
#include <string.h>

static int starts_with(const char *line, const char *pfx) {
    return strncmp(line, pfx, strlen(pfx)) == 0;
}

static int parse_bool_token(const char *s) {
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) return 1;
    return 0;
}

int mc_sms_parse_display(McSmsDisplay *out, const char *text, char *err, size_t errlen) {
    const char *p;
    char line[256];
    int in_flags = 0, in_safeing = 0;
    size_t i;

    if (!out || !text) {
        if (err && errlen) snprintf(err, errlen, "null args");
        return -1;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->safeing.safe_arm_discrete, sizeof out->safeing.safe_arm_discrete, "unknown");

    p = text;
    while (*p) {
        i = 0;
        while (p[i] && p[i] != '\n' && i + 1 < sizeof line) {
            line[i] = p[i];
            i++;
        }
        line[i] = 0;
        p += i;
        if (*p == '\n') p++;

        {
            char *s = line;
            int indent = 0;
            while (*s == ' ') {
                indent++;
                s++;
            }
            while (*s == '\t') {
                indent += 2;
                s++;
            }
            if (*s == 0 || *s == '#') continue;

            if (starts_with(s, "station_loadout_flags:")) {
                in_flags = 1;
                in_safeing = 0;
                continue;
            }
            if (starts_with(s, "safeing:")) {
                in_safeing = 1;
                in_flags = 0;
                continue;
            }
            if (indent == 0 && s[0] && s[0] != '-') {
                if (!starts_with(s, "station_loadout_flags") && !starts_with(s, "safeing")) {
                    in_flags = 0;
                    in_safeing = 0;
                }
            }

            if (in_flags && indent >= 2) {
                char id[MC_STA_ID_LEN], flag[MC_FLAG_LEN];
                char *colon = strchr(s, ':');
                if (!colon) continue;
                *colon = 0;
                snprintf(id, sizeof id, "%s", s);
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                snprintf(flag, sizeof flag, "%s", colon);
                {
                    size_t n = strlen(flag);
                    while (n > 0 && (flag[n - 1] == '\r' || flag[n - 1] == ' '))
                        flag[--n] = 0;
                }
                if (strcmp(flag, "empty") != 0 && strcmp(flag, "hung") != 0 &&
                    strcmp(flag, "selected") != 0) {
                    if (err && errlen)
                        snprintf(err, errlen, "bad loadout flag '%s' (display-only)", flag);
                    return -1;
                }
                if (out->nstations >= MC_STA_MAX) {
                    if (err && errlen) snprintf(err, errlen, "too many stations");
                    return -1;
                }
                snprintf(out->stations[out->nstations].station_id,
                         sizeof out->stations[0].station_id, "%s", id);
                snprintf(out->stations[out->nstations].flag,
                         sizeof out->stations[0].flag, "%s", flag);
                out->nstations++;
                continue;
            }

            if (in_safeing && indent >= 2) {
                char *colon = strchr(s, ':');
                char key[64], val[64];
                if (!colon) continue;
                *colon = 0;
                snprintf(key, sizeof key, "%s", s);
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                snprintf(val, sizeof val, "%s", colon);
                {
                    size_t n = strlen(val);
                    while (n > 0 && (val[n - 1] == '\r' || val[n - 1] == ' '))
                        val[--n] = 0;
                }
                if (strcmp(key, "mechanical_ground_pins") == 0)
                    out->safeing.mechanical_ground_pins = parse_bool_token(val);
                else if (strcmp(key, "wow_inhibits_release") == 0)
                    out->safeing.wow_inhibits_release = parse_bool_token(val);
                else if (strcmp(key, "dual_consent") == 0)
                    out->safeing.dual_consent = parse_bool_token(val);
                else if (strcmp(key, "fcc_envelope_veto") == 0)
                    out->safeing.fcc_envelope_veto = parse_bool_token(val);
                else if (strcmp(key, "jettison_guarded") == 0)
                    out->safeing.jettison_guarded = parse_bool_token(val);
                else if (strcmp(key, "live_fuzing_in_repo") == 0)
                    out->safeing.live_fuzing_in_repo = parse_bool_token(val);
                else if (strcmp(key, "safe_arm_discrete") == 0)
                    snprintf(out->safeing.safe_arm_discrete,
                             sizeof out->safeing.safe_arm_discrete, "%s", val);
            }
        }
    }

    if (out->nstations < 1) {
        if (err && errlen) snprintf(err, errlen, "no station_loadout_flags");
        return -1;
    }
    if (out->safeing.live_fuzing_in_repo) {
        if (err && errlen)
            snprintf(err, errlen, "live_fuzing_in_repo must be false (weapons boundary)");
        return -1;
    }
    if (err && errlen) err[0] = 0;
    return 0;
}

void mc_sms_tick(const McSmsDisplay *d, unsigned tick, McSmsTickResult *res) {
    int i, selected = 0, hung = 0;

    if (!res) return;
    memset(res, 0, sizeof *res);
    if (!d || d->nstations < 1) {
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "SMS tick %u FAIL: no display state", tick);
        return;
    }
    if (d->safeing.live_fuzing_in_repo) {
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary,
                 "SMS tick %u FAIL: weapons boundary (live_fuzing)", tick);
        return;
    }
    for (i = 0; i < d->nstations; i++) {
        if (strcmp(d->stations[i].flag, "selected") == 0) selected++;
        if (strcmp(d->stations[i].flag, "hung") == 0) hung++;
    }
    res->ok = 1;
    snprintf(res->summary, sizeof res->summary,
             "SMS tick %u PASS stations=%d hung=%d selected=%d safe_arm=%s dual_consent=%d",
             tick, d->nstations, hung, selected, d->safeing.safe_arm_discrete,
             d->safeing.dual_consent);
}

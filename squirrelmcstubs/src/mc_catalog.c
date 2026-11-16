#include "mc_catalog.h"

#include <stdio.h>
#include <string.h>

static int starts_with(const char *line, const char *pfx) {
    return strncmp(line, pfx, strlen(pfx)) == 0;
}

int mc_catalog_parse(McCatalog *out, const char *text, char *err, size_t errlen) {
    const char *p;
    char line[256];
    size_t i;

    if (!out || !text) {
        if (err && errlen) snprintf(err, errlen, "null args");
        return -1;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->aircraft_id, sizeof out->aircraft_id, "ISM-FJ-1");

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
            while (*s == ' ' || *s == '\t') s++;
            if (starts_with(s, "aircraft_id:")) {
                s += strlen("aircraft_id:");
                while (*s == ' ' || *s == '\t' || *s == '"') s++;
                {
                    char *q = s;
                    while (*q && *q != '"' && *q != ' ' && *q != '\t' && *q != '\r') q++;
                    *q = 0;
                    if (*s) snprintf(out->aircraft_id, sizeof out->aircraft_id, "%s", s);
                }
                continue;
            }
            if (starts_with(s, "- name:")) {
                s += strlen("- name:");
                while (*s == ' ' || *s == '\t') s++;
                if (*s == 0) continue;
                if (out->ntopics >= MC_TOPIC_MAX) {
                    if (err && errlen) snprintf(err, errlen, "too many topics");
                    return -1;
                }
                snprintf(out->topics[out->ntopics].name, MC_NAME_LEN, "%s", s);
                {
                    char *t = out->topics[out->ntopics].name;
                    size_t n = strlen(t);
                    while (n > 0 && (t[n - 1] == '\r' || t[n - 1] == ' ')) {
                        t[--n] = 0;
                    }
                }
                out->ntopics++;
            }
        }
    }

    if (out->ntopics < 1) {
        if (err && errlen) snprintf(err, errlen, "no topics in catalog");
        return -1;
    }
    if (err && errlen) err[0] = 0;
    return 0;
}

int mc_catalog_find(const McCatalog *cat, const char *name) {
    int i;
    if (!cat || !name) return -1;
    for (i = 0; i < cat->ntopics; i++) {
        if (strcmp(cat->topics[i].name, name) == 0) return i;
    }
    return -1;
}

int mc_catalog_has_required(const McCatalog *cat, char *err, size_t errlen) {
    static const char *need[] = {
        "mc.stores_config",
        "mc.radar_tracks",
        "mc.mode_request",
        "sms.safe_arm_discrete",
        "fcc.stores_inhibit",
        NULL
    };
    int i;
    for (i = 0; need[i]; i++) {
        if (mc_catalog_find(cat, need[i]) < 0) {
            if (err && errlen)
                snprintf(err, errlen, "missing required topic %s", need[i]);
            return -1;
        }
    }
    if (err && errlen) err[0] = 0;
    return 0;
}

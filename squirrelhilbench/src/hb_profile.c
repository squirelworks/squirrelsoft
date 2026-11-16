#define _POSIX_C_SOURCE 200809L
#include "hb_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *hb_channel_kind_name(HbChannelKind k) {
    switch (k) {
    case HB_CH_ACTUATOR:
        return "actuator";
    case HB_CH_INCEPTOR:
        return "inceptor";
    case HB_CH_BUS:
        return "bus";
    case HB_CH_STORES_DISPLAY:
        return "stores_display";
    default:
        return "unknown";
    }
}

const char *hb_health_name(HbHealth h) {
    switch (h) {
    case HB_HEALTH_OK:
        return "ok";
    case HB_HEALTH_DEGRADED:
        return "degraded";
    case HB_HEALTH_FAULT:
        return "fault";
    case HB_HEALTH_OFFLINE:
        return "offline";
    default:
        return "unknown";
    }
}

HbHealth hb_health_parse(const char *s) {
    if (!s) return HB_HEALTH_OFFLINE;
    if (strcmp(s, "ok") == 0 || strcmp(s, "healthy") == 0) return HB_HEALTH_OK;
    if (strcmp(s, "degraded") == 0) return HB_HEALTH_DEGRADED;
    if (strcmp(s, "fault") == 0 || strcmp(s, "fail") == 0) return HB_HEALTH_FAULT;
    if (strcmp(s, "offline") == 0) return HB_HEALTH_OFFLINE;
    return HB_HEALTH_OFFLINE;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

static int add_channel(HbProfile *p, HbChannelKind kind, const char *id, HbHealth h, char *err,
                       int err_cap) {
    if (p->nch >= HB_MAX_CH) {
        if (err && err_cap) snprintf(err, err_cap, "too many channels");
        return -1;
    }
    HbChannel *c = &p->ch[p->nch++];
    memset(c, 0, sizeof *c);
    snprintf(c->id, sizeof c->id, "%s", id);
    c->kind = kind;
    c->health = h;
    return 0;
}

int hb_profile_parse(HbProfile *p, const char *text, char *err, int err_cap) {
    if (!p || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(p, 0, sizeof *p);
    snprintf(p->aircraft_id, sizeof p->aircraft_id, "ISM-FJ-1");
    snprintf(p->transport, sizeof p->transport, "mock");
    snprintf(p->endpoint, sizeof p->endpoint, "mock://hil");
    p->estop_latched = 0;
    p->safeing_armed = 0;
    p->connected = 0;

    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int saw = 0, line_no = 0;
    while (line) {
        line_no++;
        char *t = trim(line);
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (!starts_with(t, "ISM-HIL bench profile")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-HIL bench profile", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[80], val[160];
        if (sscanf(t, "%79[^:]: %159[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        trim(key);
        trim(val);
        if (strcmp(key, "aircraft_id") == 0)
            snprintf(p->aircraft_id, sizeof p->aircraft_id, "%s", val);
        else if (strcmp(key, "transport") == 0)
            snprintf(p->transport, sizeof p->transport, "%s", val);
        else if (strcmp(key, "endpoint") == 0)
            snprintf(p->endpoint, sizeof p->endpoint, "%s", val);
        else if (strcmp(key, "estop") == 0) {
            if (strcmp(val, "clear") == 0 || strcmp(val, "ok") == 0) p->estop_latched = 0;
            else if (strcmp(val, "latched") == 0 || strcmp(val, "engaged") == 0)
                p->estop_latched = 1;
            else {
                if (err && err_cap) snprintf(err, err_cap, "line %d: bad estop", line_no);
                free(buf);
                return -1;
            }
        } else if (strcmp(key, "safeing") == 0) {
            /* Display / interlock state only — not arming protocol. */
            if (strcmp(val, "safe") == 0) p->safeing_armed = 0;
            else if (strcmp(val, "armed_display") == 0) p->safeing_armed = 1;
            else {
                if (err && err_cap) snprintf(err, err_cap, "line %d: bad safeing", line_no);
                free(buf);
                return -1;
            }
        } else if (starts_with(key, "actuator ")) {
            const char *id = key + 9;
            while (*id == ' ') id++;
            if (add_channel(p, HB_CH_ACTUATOR, id, hb_health_parse(val), err, err_cap) != 0) {
                free(buf);
                return -1;
            }
        } else if (starts_with(key, "inceptor ")) {
            const char *id = key + 9;
            while (*id == ' ') id++;
            if (add_channel(p, HB_CH_INCEPTOR, id, hb_health_parse(val), err, err_cap) != 0) {
                free(buf);
                return -1;
            }
        } else if (starts_with(key, "bus ")) {
            const char *id = key + 4;
            while (*id == ' ') id++;
            if (add_channel(p, HB_CH_BUS, id, hb_health_parse(val), err, err_cap) != 0) {
                free(buf);
                return -1;
            }
        } else if (starts_with(key, "stores_display ")) {
            const char *id = key + 15;
            while (*id == ' ') id++;
            if (add_channel(p, HB_CH_STORES_DISPLAY, id, hb_health_parse(val), err, err_cap) !=
                0) {
                free(buf);
                return -1;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (p->nch < 1) {
        if (err && err_cap) snprintf(err, err_cap, "profile needs channels");
        return -1;
    }
    return 0;
}

int hb_profile_connect_mock(HbProfile *p, char *err, int err_cap) {
    if (!p) {
        if (err && err_cap) snprintf(err, err_cap, "null profile");
        return -1;
    }
    if (strcmp(p->transport, "mock") != 0) {
        if (err && err_cap)
            snprintf(err, err_cap, "only mock transport in scaffold (got %s)", p->transport);
        return -1;
    }
    if (p->estop_latched) {
        if (err && err_cap) snprintf(err, err_cap, "estop latched — refuse connect");
        return -1;
    }
    p->connected = 1;
    return 0;
}

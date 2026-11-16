#define _POSIX_C_SOURCE 200809L
#include "ht_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

const char *ht_page_type_name(HtPageType t) {
    return t == HT_PAGE_MFD ? "mfd" : "hud";
}

const char *ht_station_flag_name(HtStationFlag f) {
    switch (f) {
    case HT_STATION_EMPTY:
        return "empty";
    case HT_STATION_LOADED:
        return "loaded";
    case HT_STATION_SELECTED:
        return "selected";
    case HT_STATION_UNSAFE:
        return "unsafe";
    default:
        return "empty";
    }
}

int ht_station_flag_parse(const char *s, HtStationFlag *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "empty") == 0) {
        *out = HT_STATION_EMPTY;
        return 0;
    }
    if (strcmp(s, "loaded") == 0) {
        *out = HT_STATION_LOADED;
        return 0;
    }
    if (strcmp(s, "selected") == 0) {
        *out = HT_STATION_SELECTED;
        return 0;
    }
    if (strcmp(s, "unsafe") == 0) {
        *out = HT_STATION_UNSAFE;
        return 0;
    }
    return -1;
}

void ht_page_clear(HtPage *p) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->width = 1024;
    p->height = 768;
}

void ht_page_new_hud(HtPage *p, const char *name) {
    HtElement e;
    ht_page_clear(p);
    p->type = HT_PAGE_HUD;
    snprintf(p->name, sizeof p->name, "%s", name && *name ? name : "primary-flight");
    p->width = 1024;
    p->height = 768;

    memset(&e, 0, sizeof e);
    snprintf(e.id, sizeof e.id, "attitude");
    snprintf(e.kind, sizeof e.kind, "attitude");
    e.x = 412;
    e.y = 284;
    e.w = 200;
    e.h = 200;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "airspeed");
    snprintf(e.kind, sizeof e.kind, "airspeed");
    e.x = 120;
    e.y = 284;
    e.w = 80;
    e.h = 200;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "altitude");
    snprintf(e.kind, sizeof e.kind, "altitude");
    e.x = 824;
    e.y = 284;
    e.w = 80;
    e.h = 200;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "heading");
    snprintf(e.kind, sizeof e.kind, "heading");
    e.x = 392;
    e.y = 64;
    e.w = 240;
    e.h = 40;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "aoa");
    snprintf(e.kind, sizeof e.kind, "aoa");
    e.x = 220;
    e.y = 300;
    e.w = 40;
    e.h = 80;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "gear");
    snprintf(e.kind, sizeof e.kind, "gear_status");
    e.x = 80;
    e.y = 700;
    e.w = 60;
    e.h = 40;
    ht_page_add_element(p, &e, NULL, 0);

    /* Display-only station flags (symbology). No weapons internals. */
    snprintf(e.id, sizeof e.id, "sta1");
    snprintf(e.kind, sizeof e.kind, "stores_station");
    e.x = 40;
    e.y = 40;
    e.w = 24;
    e.h = 24;
    e.station = 1;
    e.station_flag = HT_STATION_EMPTY;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "sta2");
    e.x = 72;
    e.station = 2;
    e.station_flag = HT_STATION_LOADED;
    ht_page_add_element(p, &e, NULL, 0);
}

void ht_page_new_mfd(HtPage *p, const char *name) {
    HtElement e;
    ht_page_clear(p);
    p->type = HT_PAGE_MFD;
    snprintf(p->name, sizeof p->name, "%s", name && *name ? name : "systems-synoptic");
    p->width = 1280;
    p->height = 800;

    memset(&e, 0, sizeof e);
    snprintf(e.id, sizeof e.id, "radar");
    snprintf(e.kind, sizeof e.kind, "radar_plan");
    e.x = 40;
    e.y = 40;
    e.w = 600;
    e.h = 520;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "eo");
    snprintf(e.kind, sizeof e.kind, "eo_video");
    e.x = 660;
    e.y = 40;
    e.w = 560;
    e.h = 320;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "caution");
    snprintf(e.kind, sizeof e.kind, "caution_panel");
    e.x = 660;
    e.y = 380;
    e.w = 560;
    e.h = 180;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "fuel");
    snprintf(e.kind, sizeof e.kind, "fuel_synoptic");
    e.x = 40;
    e.y = 580;
    e.w = 400;
    e.h = 180;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "engines");
    snprintf(e.kind, sizeof e.kind, "engine_synoptic");
    e.x = 460;
    e.y = 580;
    e.w = 400;
    e.h = 180;
    ht_page_add_element(p, &e, NULL, 0);

    snprintf(e.id, sizeof e.id, "menu");
    snprintf(e.kind, sizeof e.kind, "bezel_menu");
    e.x = 880;
    e.y = 580;
    e.w = 340;
    e.h = 180;
    ht_page_add_element(p, &e, NULL, 0);
}

int ht_page_add_element(HtPage *p, const HtElement *el, char *err, int err_cap) {
    if (!p || !el) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    if (p->nel >= HT_MAX_ELEMENTS) {
        if (err && err_cap) snprintf(err, err_cap, "too many elements");
        return -1;
    }
    if (!el->id[0] || !el->kind[0]) {
        if (err && err_cap) snprintf(err, err_cap, "element needs id and kind");
        return -1;
    }
    if (el->w <= 0 || el->h <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "element needs positive size");
        return -1;
    }
    p->el[p->nel++] = *el;
    return 0;
}

static int parse_element_line(HtElement *el, const char *line, char *err, int err_cap, int line_no) {
    char id[HT_NAME_LEN];
    const char *rest;
    char *dup;
    char *tok;
    char *save = NULL;

    memset(el, 0, sizeof *el);
    if (sscanf(line, "element %63[^:]:", id) != 1) {
        if (err && err_cap) snprintf(err, err_cap, "line %d: element id", line_no);
        return -1;
    }
    snprintf(el->id, sizeof el->id, "%s", id);
    rest = strchr(line, ':');
    if (!rest) {
        if (err && err_cap) snprintf(err, err_cap, "line %d: missing :", line_no);
        return -1;
    }
    rest++;
    while (*rest == ' ' || *rest == '\t') rest++;
    dup = strdup(rest);
    if (!dup) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    for (tok = strtok_r(dup, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
        char key[64], val[96];
        if (sscanf(tok, "%63[^=]=%95s", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: bad field %s", line_no, tok);
            free(dup);
            return -1;
        }
        if (strcmp(key, "kind") == 0) snprintf(el->kind, sizeof el->kind, "%s", val);
        else if (strcmp(key, "x") == 0) el->x = atoi(val);
        else if (strcmp(key, "y") == 0) el->y = atoi(val);
        else if (strcmp(key, "w") == 0) el->w = atoi(val);
        else if (strcmp(key, "h") == 0) el->h = atoi(val);
        else if (strcmp(key, "station") == 0) el->station = atoi(val);
        else if (strcmp(key, "state") == 0) {
            if (ht_station_flag_parse(val, &el->station_flag) != 0) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: bad state", line_no);
                free(dup);
                return -1;
            }
        } else {
            if (err && err_cap) snprintf(err, err_cap, "line %d: unknown field %s", line_no, key);
            free(dup);
            return -1;
        }
    }
    free(dup);
    if (!el->kind[0] || el->w <= 0 || el->h <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "line %d: need kind w h", line_no);
        return -1;
    }
    return 0;
}

int ht_page_parse(HtPage *p, const char *text, char *err, int err_cap) {
    char *buf;
    char *save = NULL;
    char *line;
    int saw = 0, line_no = 0;

    if (!p || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    ht_page_clear(p);
    buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *t;
        line_no++;
        t = trim(line);
        if (!*t || *t == '#') continue;
        if (!saw) {
            if (!starts_with(t, "SQHUD page")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need SQHUD page header", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            continue;
        }
        if (starts_with(t, "element ")) {
            HtElement el;
            if (parse_element_line(&el, t, err, err_cap, line_no) != 0) {
                free(buf);
                return -1;
            }
            if (ht_page_add_element(p, &el, err, err_cap) != 0) {
                free(buf);
                return -1;
            }
            continue;
        }
        {
            char key[64], val[160];
            if (sscanf(t, "%63[^:]: %159[^\n]", key, val) != 2) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
                free(buf);
                return -1;
            }
            trim(key);
            trim(val);
            if (strcmp(key, "type") == 0) {
                if (strcmp(val, "hud") == 0) p->type = HT_PAGE_HUD;
                else if (strcmp(val, "mfd") == 0) p->type = HT_PAGE_MFD;
                else {
                    if (err && err_cap) snprintf(err, err_cap, "line %d: type hud|mfd", line_no);
                    free(buf);
                    return -1;
                }
            } else if (strcmp(key, "name") == 0) {
                snprintf(p->name, sizeof p->name, "%s", val);
            } else if (strcmp(key, "width") == 0) {
                p->width = atoi(val);
            } else if (strcmp(key, "height") == 0) {
                p->height = atoi(val);
            } else {
                if (err && err_cap) snprintf(err, err_cap, "line %d: unknown key %s", line_no, key);
                free(buf);
                return -1;
            }
        }
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (!p->name[0] || p->width <= 0 || p->height <= 0 || p->nel < 1) {
        if (err && err_cap) snprintf(err, err_cap, "need name, size, and >=1 element");
        return -1;
    }
    return 0;
}

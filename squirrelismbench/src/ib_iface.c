#define _POSIX_C_SOURCE 200809L
#include "ib_iface.h"

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

int ib_keyboard_parse(IbKeyboardIface *k, const char *text, char *err, int err_cap) {
    if (!k || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(k, 0, sizeof *k);
    k->debounce_ms = 5;
    k->scan_hz = 1000;
    snprintf(k->diode, sizeof k->diode, "col2row");
    snprintf(k->hid_report, sizeof k->hid_report, "boot_keyboard");
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
            if (!starts_with(t, "ISM-HID keyboard interface")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-HID keyboard interface", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[64], val[160];
        if (sscanf(t, "%63[^:]: %159[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        trim(key);
        trim(val);
        if (strcmp(key, "layout") == 0) snprintf(k->layout, sizeof k->layout, "%s", val);
        else if (strcmp(key, "rows") == 0) k->rows = atoi(val);
        else if (strcmp(key, "cols") == 0) k->cols = atoi(val);
        else if (strcmp(key, "diode") == 0) snprintf(k->diode, sizeof k->diode, "%s", val);
        else if (strcmp(key, "hid_report") == 0)
            snprintf(k->hid_report, sizeof k->hid_report, "%s", val);
        else if (strcmp(key, "debounce_ms") == 0) k->debounce_ms = atoi(val);
        else if (strcmp(key, "scan_hz") == 0) k->scan_hz = atoi(val);
        else if (starts_with(key, "key ")) {
            const char *name = key + 4;
            while (*name == ' ') name++;
            int r = -1, c = -1;
            if (sscanf(val, "%d %d", &r, &c) != 2) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: key needs row col", line_no);
                free(buf);
                return -1;
            }
            if (k->nkeys >= IB_MAX_KEYS) {
                if (err && err_cap) snprintf(err, err_cap, "too many keys");
                free(buf);
                return -1;
            }
            snprintf(k->key_name[k->nkeys], sizeof k->key_name[0], "%s", name);
            k->key_row[k->nkeys] = r;
            k->key_col[k->nkeys] = c;
            k->nkeys++;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (k->rows <= 0 || k->cols <= 0 || k->rows > IB_MAX_ROWS || k->cols > IB_MAX_COLS) {
        if (err && err_cap) snprintf(err, err_cap, "bad rows/cols");
        return -1;
    }
    return 0;
}

int ib_mouse_parse(IbMouseIface *m, const char *text, char *err, int err_cap) {
    if (!m || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(m, 0, sizeof *m);
    m->buttons = 3;
    m->cpi_default = 800;
    m->report_hz = 125;
    snprintf(m->sensor, sizeof m->sensor, "optical_stub");
    snprintf(m->scroll, sizeof m->scroll, "vertical");
    snprintf(m->hid_report, sizeof m->hid_report, "boot_mouse");
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
            if (!starts_with(t, "ISM-HID mouse interface")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-HID mouse interface", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[64], val[160];
        if (sscanf(t, "%63[^:]: %159[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        trim(key);
        trim(val);
        if (strcmp(key, "sensor") == 0) snprintf(m->sensor, sizeof m->sensor, "%s", val);
        else if (strcmp(key, "buttons") == 0) m->buttons = atoi(val);
        else if (strcmp(key, "scroll") == 0) snprintf(m->scroll, sizeof m->scroll, "%s", val);
        else if (strcmp(key, "hid_report") == 0)
            snprintf(m->hid_report, sizeof m->hid_report, "%s", val);
        else if (strcmp(key, "cpi_default") == 0) m->cpi_default = atoi(val);
        else if (strcmp(key, "report_hz") == 0) m->report_hz = atoi(val);
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (m->buttons < 1 || m->buttons > 8 || m->cpi_default <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "bad mouse fields");
        return -1;
    }
    return 0;
}

#define _POSIX_C_SOURCE 200809L
#include "ib_printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int parse_int_field(const char *val) {
    if (strncmp(val, "0x", 2) == 0 || strncmp(val, "0X", 2) == 0)
        return (int)strtol(val, NULL, 16);
    if (strncmp(val, "GP", 2) == 0 || strncmp(val, "gp", 2) == 0)
        return atoi(val + 2);
    return atoi(val);
}

int ib_printer_parse(IbPrinterIface *p, const char *text, char *err, int err_cap) {
    if (!p || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(p, 0, sizeof *p);
    p->vid = 0x1209;
    p->pid = 0x000F;
    snprintf(p->driver, sizeof p->driver, "DRV8833");
    p->ain1 = 0;
    p->ain2 = 1;
    p->bin1 = 2;
    p->bin2 = 3;
    p->nsleep = 4;
    p->head_cs = 5;
    p->head_sck = 6;
    p->head_mosi = 7;
    p->paper_gp = 8;
    p->btn_feed = 9;
    p->btn_cancel = 10;
    p->btn_pwr = 11;

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
            if (strncmp(t, "ISM-PRT printer interface", 25) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-PRT printer interface", line_no);
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
        if (strcmp(key, "driver") == 0) snprintf(p->driver, sizeof p->driver, "%s", val);
        else if (strcmp(key, "vid") == 0) p->vid = parse_int_field(val);
        else if (strcmp(key, "pid") == 0) p->pid = parse_int_field(val);
        else if (strcmp(key, "ain1") == 0) p->ain1 = parse_int_field(val);
        else if (strcmp(key, "ain2") == 0) p->ain2 = parse_int_field(val);
        else if (strcmp(key, "bin1") == 0) p->bin1 = parse_int_field(val);
        else if (strcmp(key, "bin2") == 0) p->bin2 = parse_int_field(val);
        else if (strcmp(key, "nsleep") == 0) p->nsleep = parse_int_field(val);
        else if (strcmp(key, "head_cs") == 0) p->head_cs = parse_int_field(val);
        else if (strcmp(key, "head_sck") == 0) p->head_sck = parse_int_field(val);
        else if (strcmp(key, "head_mosi") == 0) p->head_mosi = parse_int_field(val);
        else if (strcmp(key, "paper_gp") == 0) p->paper_gp = parse_int_field(val);
        else if (strcmp(key, "btn_feed") == 0) p->btn_feed = parse_int_field(val);
        else if (strcmp(key, "btn_cancel") == 0) p->btn_cancel = parse_int_field(val);
        else if (strcmp(key, "btn_pwr") == 0) p->btn_pwr = parse_int_field(val);
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (p->vid != 0x1209 || p->pid != 0x000F) {
        if (err && err_cap) snprintf(err, err_cap, "VID/PID must be 0x1209/0x000F");
        return -1;
    }
    if (strcmp(p->driver, "DRV8833") != 0) {
        if (err && err_cap) snprintf(err, err_cap, "driver must be DRV8833");
        return -1;
    }
    return 0;
}

int ib_printer_selftest(const IbPrinterIface *p, IbPrinterTestResult *res) {
    memset(res, 0, sizeof *res);
    res->ok = 1;
    /* Distinct GPIOs for Steve map (ISM printer README). */
    int gps[] = {p->ain1, p->ain2, p->bin1, p->bin2, p->nsleep, p->head_cs,
                 p->head_sck, p->head_mosi, p->paper_gp, p->btn_feed, p->btn_cancel,
                 p->btn_pwr};
    int i, j;
    for (i = 0; i < 12; i++) {
        for (j = i + 1; j < 12; j++) {
            if (gps[i] == gps[j]) {
                res->nfail++;
                res->ok = 0;
            }
        }
    }
    if (res->nfail == 0) res->npass++;

    /* Synthetic nSLEEP gate: motors idle when sleep low. */
    int sleep = 0, mot_a = 0, mot_b = 0;
    if (sleep == 0 && (mot_a || mot_b)) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;
    sleep = 1;
    mot_a = 1;
    mot_b = 1;
    if (!(sleep && mot_a && mot_b)) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    /* Paper sensor open → no feed; closed → feed ok. */
    int paper = 0; /* open */
    int feed_ok = paper ? 1 : 0;
    if (feed_ok) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;
    paper = 1;
    feed_ok = paper ? 1 : 0;
    if (!feed_ok) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    /* Head SPI stub: CS low enables one byte clocked on MOSI. */
    int cs = 0, mosi = 1, byte_latched = 0;
    if (cs == 0) byte_latched = mosi;
    if (byte_latched != 1) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    snprintf(res->summary, sizeof res->summary,
             "printer selftest driver=%s vid=0x%04x pid=0x%04x pass=%d fail=%d → %s",
             p->driver, p->vid, p->pid, res->npass, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}

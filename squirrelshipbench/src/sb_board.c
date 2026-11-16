#define _POSIX_C_SOURCE 200809L
#include "sb_board.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int role_ok(const char *role) {
    return strstr(role, "weapon") == NULL && strstr(role, "fire_control") == NULL &&
           strstr(role, "seeker") == NULL && strstr(role, "guidance") == NULL &&
           strstr(role, "warhead") == NULL && strstr(role, "fuzing") == NULL &&
           strstr(role, "arming") == NULL;
}

int sb_board_parse(SbBoardIface *b, const char *text, char *err, int err_cap) {
    if (!b || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(b, 0, sizeof *b);
    b->no_weapons = 1;
    b->scale_model_only = 1;
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
            if (strncmp(t, "ISM-WS control-board", 20) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-WS control-board", line_no);
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
        if (strcmp(key, "board_id") == 0) snprintf(b->board_id, sizeof b->board_id, "%s", val);
        else if (strcmp(key, "no_weapons") == 0) b->no_weapons = atoi(val);
        else if (strcmp(key, "scale_model_only") == 0) b->scale_model_only = atoi(val);
        else if (strcmp(key, "channel") == 0) {
            char id[32], role[48];
            if (sscanf(val, "%31s %47s", id, role) != 2) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: channel ID ROLE", line_no);
                free(buf);
                return -1;
            }
            if (b->nch >= SB_MAX_CH) {
                if (err && err_cap) snprintf(err, err_cap, "too many channels");
                free(buf);
                return -1;
            }
            snprintf(b->ch_id[b->nch], sizeof b->ch_id[0], "%s", id);
            snprintf(b->ch_role[b->nch], sizeof b->ch_role[0], "%s", role);
            b->nch++;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw || b->nch < 1) {
        if (err && err_cap) snprintf(err, err_cap, "incomplete control-board");
        return -1;
    }
    return 0;
}

int sb_board_selftest(const SbBoardIface *b, SbBoardResult *res) {
    int i;
    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!b->no_weapons || !b->scale_model_only) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;
    for (i = 0; i < b->nch; i++) {
        if (!role_ok(b->ch_role[i])) {
            res->nfail++;
            res->ok = 0;
        } else
            res->npass++;
    }
    snprintf(res->summary, sizeof res->summary,
             "warship board id=%s channels=%d pass=%d fail=%d → %s", b->board_id, b->nch,
             res->npass, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}

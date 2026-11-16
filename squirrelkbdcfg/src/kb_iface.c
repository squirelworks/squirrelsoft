#include "kb_iface.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip(char *s)
{
    size_t n;
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
}

int kb_iface_load(const char *path, KbIface *out, char *err, int errlen)
{
    FILE *f;
    char line[256];

    if (!path || !out) {
        if (err && errlen > 0)
            snprintf(err, errlen, "null arg");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->path, sizeof(out->path), "%s", path);
    f = fopen(path, "r");
    if (!f) {
        if (err && errlen > 0)
            snprintf(err, errlen, "open %s", path);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        strip(p);
        if (p[0] == 0 || p[0] == '#')
            continue;
        if (strncmp(p, "ISM-HID", 7) == 0)
            continue;
        if (strncmp(p, "layout:", 7) == 0) {
            char *v = p + 7;
            strip(v);
            snprintf(out->layout, sizeof(out->layout), "%s", v);
            continue;
        }
        if (strncmp(p, "rows:", 5) == 0) {
            out->rows = atoi(p + 5);
            continue;
        }
        if (strncmp(p, "cols:", 5) == 0) {
            out->cols = atoi(p + 5);
            continue;
        }
        if (strncmp(p, "debounce_ms:", 12) == 0) {
            out->debounce_ms = atoi(p + 12);
            continue;
        }
        if (strncmp(p, "scan_hz:", 8) == 0) {
            out->scan_hz = atoi(p + 8);
            continue;
        }
        if (strncmp(p, "key ", 4) == 0) {
            /* Fixture form: "key ESC: 0 0" (colon after name, like ismbench). */
            char name[KB_NAME_LEN];
            char *colon;
            int r = 0, c = 0;
            if (out->nkeys >= KB_MAX_KEYS)
                continue;
            colon = strchr(p + 4, ':');
            if (!colon)
                continue;
            {
                size_t nlen = (size_t)(colon - (p + 4));
                if (nlen == 0 || nlen >= sizeof(name))
                    continue;
                memcpy(name, p + 4, nlen);
                name[nlen] = 0;
                strip(name);
            }
            if (sscanf(colon + 1, "%d %d", &r, &c) == 2) {
                KbKey *k = &out->keys[out->nkeys++];
                snprintf(k->name, sizeof(k->name), "%s", name);
                k->row = r;
                k->col = c;
            }
            continue;
        }
    }
    fclose(f);
    if (out->rows <= 0 || out->cols <= 0 || out->nkeys <= 0) {
        if (err && errlen > 0)
            snprintf(err, errlen, "incomplete iface");
        return -1;
    }
    return 0;
}

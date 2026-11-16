#define _POSIX_C_SOURCE 200809L
#include "ht_export.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int appendf(char *buf, int cap, int *n, const char *fmt, ...) {
    va_list ap;
    int left, w;
    if (!buf || !n || cap <= 0) return -1;
    left = cap - *n;
    if (left <= 1) return -1;
    va_start(ap, fmt);
    w = vsnprintf(buf + *n, (size_t)left, fmt, ap);
    va_end(ap);
    if (w < 0 || w >= left) return -1;
    *n += w;
    return 0;
}

int ht_export_json(const HtPage *p, char *buf, int cap) {
    int n = 0, i;
    if (!p || !buf || cap < 8) return -1;
    buf[0] = 0;
    if (appendf(buf, cap, &n,
                "{\n  \"format\": \"sqhud\",\n  \"version\": 1,\n"
                "  \"type\": \"%s\",\n  \"name\": \"%s\",\n"
                "  \"width\": %d,\n  \"height\": %d,\n  \"elements\": [\n",
                ht_page_type_name(p->type), p->name, p->width, p->height) != 0)
        return -1;
    for (i = 0; i < p->nel; i++) {
        const HtElement *e = &p->el[i];
        if (appendf(buf, cap, &n,
                    "    {\"id\": \"%s\", \"kind\": \"%s\", "
                    "\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d",
                    e->id, e->kind, e->x, e->y, e->w, e->h) != 0)
            return -1;
        if (e->station > 0) {
            if (appendf(buf, cap, &n, ", \"station\": %d, \"state\": \"%s\"", e->station,
                        ht_station_flag_name(e->station_flag)) != 0)
                return -1;
        }
        if (appendf(buf, cap, &n, "}%s\n", i + 1 < p->nel ? "," : "") != 0) return -1;
    }
    if (appendf(buf, cap, &n, "  ]\n}\n") != 0) return -1;
    return n;
}

int ht_export_text(const HtPage *p, char *buf, int cap) {
    int n = 0, i;
    if (!p || !buf || cap < 8) return -1;
    buf[0] = 0;
    if (appendf(buf, cap, &n,
                "SQHUD page 1\n"
                "type: %s\n"
                "name: %s\n"
                "width: %d\n"
                "height: %d\n",
                ht_page_type_name(p->type), p->name, p->width, p->height) != 0)
        return -1;
    for (i = 0; i < p->nel; i++) {
        const HtElement *e = &p->el[i];
        if (e->station > 0) {
            if (appendf(buf, cap, &n,
                        "element %s: kind=%s x=%d y=%d w=%d h=%d station=%d state=%s\n", e->id,
                        e->kind, e->x, e->y, e->w, e->h, e->station,
                        ht_station_flag_name(e->station_flag)) != 0)
                return -1;
        } else {
            if (appendf(buf, cap, &n, "element %s: kind=%s x=%d y=%d w=%d h=%d\n", e->id, e->kind,
                        e->x, e->y, e->w, e->h) != 0)
                return -1;
        }
    }
    return n;
}

int ht_write_file(const char *path, const char *buf) {
    FILE *f;
    size_t n;
    if (!path || !buf) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;
    n = strlen(buf);
    if (fwrite(buf, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

#include "live_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define SQ_MKDIR(p) _mkdir(p)
#define SQ_SEP '\\'
#else
#define SQ_MKDIR(p) mkdir((p), 0755)
#define SQ_SEP '/'
#endif


int live_prefs_path(char *buf, size_t n) {
    const char *over = getenv("SQSOUND_PREFS");
    int w;
    if (over && *over) {
        w = snprintf(buf, n, "%s", over);
        return (w < 0 || (size_t)w >= n) ? -1 : 0;
    }
#ifdef _WIN32
    const char *base = getenv("APPDATA");
    if (!base || !*base) return -1;
    w = snprintf(buf, n, "%s\\squirrelsound\\live.conf", base);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) w = snprintf(buf, n, "%s/squirrelsound/live.conf", xdg);
    else if (home && *home) w = snprintf(buf, n, "%s/.config/squirrelsound/live.conf", home);
    else return -1;
#endif
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

int live_prefs_get(const char *path, const char *key, char *val, size_t n) {
    if (!path || !key || !val || n == 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    size_t kl = strlen(key);
    int rc = -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, key, kl) != 0 || line[kl] != '=') continue;
        int w = snprintf(val, n, "%s", line + kl + 1);
        rc = (w < 0 || (size_t)w >= n) ? -1 : 0; /* the last one wins */
    }
    fclose(f);
    return rc;
}

int live_prefs_load_direction(const char *path, LiveDirection *out) {
    char v[32];
    if (!out || live_prefs_get(path, "direction", v, sizeof v) != 0) return -1;
    if (strcmp(v, "waterfall") != 0 && strcmp(v, "fountain") != 0) return -1;
    *out = live_direction_parse(v, LIVE_DIR_WATERFALL);
    return 0;
}

/* mkdir -p for the directory part of path. */
static void make_parent_dirs(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof tmp) return;
    memcpy(tmp, path, len + 1);
    char *last = strrchr(tmp, SQ_SEP);
    if (!last || last == tmp) return;
    *last = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == SQ_SEP) {
            *p = '\0';
            (void)SQ_MKDIR(tmp);
            *p = SQ_SEP;
        }
    }
    (void)SQ_MKDIR(tmp);
}

int live_prefs_set(const char *path, const char *key, const char *value) {
    if (!path || !key || !*key || !value || strpbrk(key, "=\r\n") || strpbrk(value, "\r\n"))
        return -1;
    size_t kl = strlen(key);
    /* Keep other keys: read existing lines, drop the old key=. */
    char *kept = NULL;
    size_t kept_len = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, key, kl) == 0 && line[kl] == '=') continue;
            size_t l = strlen(line);
            char *grown = (char *)realloc(kept, kept_len + l + 1);
            if (!grown) {
                free(kept);
                fclose(f);
                return -1;
            }
            kept = grown;
            memcpy(kept + kept_len, line, l + 1);
            kept_len += l;
        }
        fclose(f);
    }
    make_parent_dirs(path);
    f = fopen(path, "w");
    if (!f) {
        free(kept);
        return -1;
    }
    if (kept_len) {
        fputs(kept, f);
        if (kept[kept_len - 1] != '\n') fputc('\n', f);
    }
    fprintf(f, "%s=%s\n", key, value);
    free(kept);
    return fclose(f) == 0 ? 0 : -1;
}

int live_prefs_save_direction(const char *path, LiveDirection d) {
    return live_prefs_set(path, "direction", live_direction_name(d));
}

/* cal.<source>: the source name with '=' and spaces made safe */
static void cal_key(char *key, size_t n, const char *source) {
    snprintf(key, n, "cal.%s", source && *source ? source : "default");
    for (char *p = key; *p; p++)
        if (*p == '=' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') *p = '_';
}

int live_prefs_load_cal(const char *path, const char *source, double *db) {
    char key[300], v[64], *end;
    cal_key(key, sizeof key, source);
    if (!db || live_prefs_get(path, key, v, sizeof v) != 0) return -1;
    double x = strtod(v, &end);
    if (end == v || *end || x != x || x < -100 || x > 100) return -1;
    *db = x;
    return 0;
}

int live_prefs_save_cal(const char *path, const char *source, double db) {
    char key[300], v[64];
    cal_key(key, sizeof key, source);
    snprintf(v, sizeof v, "%.2f", db);
    return live_prefs_set(path, key, v);
}

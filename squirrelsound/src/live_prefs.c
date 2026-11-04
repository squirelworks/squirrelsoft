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

#define DIRECTION_KEY "direction="

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

int live_prefs_load_direction(const char *path, LiveDirection *out) {
    if (!path || !out) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    int rc = -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, DIRECTION_KEY, strlen(DIRECTION_KEY)) != 0) continue;
        const char *v = line + strlen(DIRECTION_KEY);
        if (strcmp(v, "waterfall") == 0 || strcmp(v, "fountain") == 0) {
            *out = live_direction_parse(v, LIVE_DIR_WATERFALL);
            rc = 0;
        }
    }
    fclose(f);
    return rc;
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

int live_prefs_save_direction(const char *path, LiveDirection d) {
    if (!path) return -1;
    /* Keep other keys: read existing lines, drop old direction=. */
    char *kept = NULL;
    size_t kept_len = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, DIRECTION_KEY, strlen(DIRECTION_KEY)) == 0) continue;
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
    fprintf(f, "%s%s\n", DIRECTION_KEY, live_direction_name(d));
    free(kept);
    return fclose(f) == 0 ? 0 : -1;
}

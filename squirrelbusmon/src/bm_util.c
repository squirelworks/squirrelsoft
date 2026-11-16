#define _POSIX_C_SOURCE 200809L
#include "bm_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *bm_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

int bm_starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

int bm_parse_kv(const char *tail, char out_keys[][BM_NAME_LEN], char out_vals[][BM_FIELD_LEN],
                int cap) {
    if (!tail || !out_keys || !out_vals || cap <= 0) return -1;
    char buf[BM_LINE_LEN];
    snprintf(buf, sizeof buf, "%s", tail);
    int n = 0;
    char *save = NULL;
    char *tok = strtok_r(buf, " \t", &save);
    while (tok) {
        char *eq = strchr(tok, '=');
        if (!eq || eq == tok) {
            tok = strtok_r(NULL, " \t", &save);
            continue;
        }
        *eq = 0;
        if (n >= cap) return n;
        snprintf(out_keys[n], BM_NAME_LEN, "%s", tok);
        snprintf(out_vals[n], BM_FIELD_LEN, "%s", eq + 1);
        n++;
        tok = strtok_r(NULL, " \t", &save);
    }
    return n;
}

int bm_read_file(const char *path, char **out) {
    FILE *f;
    long n;
    char *b;
    if (!path || !out) return -1;
    *out = NULL;
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        return -1;
    }
    rewind(f);
    b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return -1;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return -1;
    }
    b[n] = 0;
    fclose(f);
    *out = b;
    return 0;
}

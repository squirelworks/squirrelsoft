/*
 * Shared helpers for SquirrelBusMon (ISM avionics data-bus analyzer).
 */
#ifndef BM_UTIL_H
#define BM_UTIL_H

#include <stddef.h>

#define BM_NAME_LEN 64
#define BM_FIELD_LEN 96
#define BM_MSG_LEN 240
#define BM_LINE_LEN 512
#define BM_MAX_TOPICS 64
#define BM_MAX_FRAMES 256
#define BM_MAX_FIELDS 16

char *bm_trim(char *s);
int bm_starts_with(const char *s, const char *p);
/* Parse key=value tokens from a space-separated tail into out_keys/out_vals. */
int bm_parse_kv(const char *tail, char out_keys[][BM_NAME_LEN], char out_vals[][BM_FIELD_LEN],
                int cap);
/* Read entire file into a malloc'd NUL-terminated buffer. Returns 0 on success. */
int bm_read_file(const char *path, char **out);

#endif

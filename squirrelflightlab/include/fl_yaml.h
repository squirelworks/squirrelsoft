/*
 * Minimal YAML subset for ISM-FJ interface documents (original Soft code).
 * Supports: block maps/seqs, plain/quoted scalars, inline {} / [], comments,
 * folded ">" blocks. Not a general YAML 1.2 implementation.
 */
#ifndef FL_YAML_H
#define FL_YAML_H

#include <stddef.h>

#define FL_YAML_MAX_NODES 4096
#define FL_YAML_KEY_LEN 96
#define FL_YAML_STR_LEN 512

typedef enum {
    FL_Y_NULL = 0,
    FL_Y_BOOL,
    FL_Y_NUM,
    FL_Y_STR,
    FL_Y_SEQ,
    FL_Y_MAP
} FlYKind;

typedef struct FlYNode {
    FlYKind kind;
    char key[FL_YAML_KEY_LEN]; /* set when this node is a map entry */
    char str[FL_YAML_STR_LEN];
    double num;
    int boolean;
    struct FlYNode *child; /* first child (map entries or seq items) */
    struct FlYNode *next;  /* next sibling */
} FlYNode;

typedef struct {
    FlYNode nodes[FL_YAML_MAX_NODES];
    int n;
    FlYNode *root;
} FlYDoc;

/* Parse text into doc->root (MAP). Returns 0 on success. */
int fl_yaml_parse(FlYDoc *doc, const char *text, char *err, int err_cap);

/* Map helpers. */
FlYNode *fl_yaml_map_get(const FlYNode *map, const char *key);
const char *fl_yaml_as_cstr(const FlYNode *n); /* STR/NUM/BOOL/NULL → buf in node or static */
int fl_yaml_as_double(const FlYNode *n, double *out);
int fl_yaml_is_map(const FlYNode *n);
int fl_yaml_is_seq(const FlYNode *n);

/* Walk map entries: first entry is map->child; siblings via ->next. */
int fl_yaml_map_count(const FlYNode *map);

#endif

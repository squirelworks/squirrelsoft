#define _POSIX_C_SOURCE 200809L
#include "fl_yaml.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, int err_cap, const char *msg) {
    if (err && err_cap > 0) snprintf(err, err_cap, "%s", msg);
}

static FlYNode *alloc_node(FlYDoc *doc) {
    if (doc->n >= FL_YAML_MAX_NODES) return NULL;
    FlYNode *n = &doc->nodes[doc->n++];
    memset(n, 0, sizeof *n);
    return n;
}

static int indent_of(const char *s) {
    int n = 0;
    while (*s == ' ') {
        n++;
        s++;
    }
    if (*s == '\t') return -1; /* tabs rejected for simplicity */
    return n;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void rtrim_buf(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = 0;
}

static void strip_inline_comment(char *s) {
    int in_sq = 0, in_dq = 0, in_flow = 0;
    for (char *p = s; *p; p++) {
        if (*p == '\'' && !in_dq) in_sq = !in_sq;
        else if (*p == '"' && !in_sq) in_dq = !in_dq;
        else if (!in_sq && !in_dq) {
            if (*p == '{' || *p == '[') in_flow++;
            else if (*p == '}' || *p == ']') {
                if (in_flow > 0) in_flow--;
            } else if (*p == '#' && in_flow == 0) {
                *p = 0;
                break;
            }
        }
    }
    rtrim_buf(s);
}

static void set_null(FlYNode *n) {
    n->kind = FL_Y_NULL;
    n->str[0] = 0;
}

static void set_bool(FlYNode *n, int v) {
    n->kind = FL_Y_BOOL;
    n->boolean = v;
    snprintf(n->str, sizeof n->str, "%s", v ? "true" : "false");
}

static void set_num(FlYNode *n, double v) {
    n->kind = FL_Y_NUM;
    n->num = v;
    snprintf(n->str, sizeof n->str, "%.17g", v);
}

static void set_str(FlYNode *n, const char *s) {
    n->kind = FL_Y_STR;
    snprintf(n->str, sizeof n->str, "%s", s ? s : "");
}

static int looks_number(const char *s) {
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    int digits = 0, dots = 0;
    for (; *p; p++) {
        if (isdigit((unsigned char)*p)) digits++;
        else if (*p == '.') dots++;
        else if (*p == 'e' || *p == 'E') {
            p++;
            if (*p == '+' || *p == '-') p++;
            if (!isdigit((unsigned char)*p)) return 0;
            while (isdigit((unsigned char)*p)) p++;
            return *p == 0 && digits > 0;
        } else
            return 0;
    }
    return digits > 0 && dots <= 1;
}

static void parse_scalar(FlYNode *n, const char *raw) {
    const char *s = skip_ws(raw);
    if (!*s || strcmp(s, "null") == 0 || strcmp(s, "~") == 0) {
        set_null(n);
        return;
    }
    if (strcmp(s, "true") == 0 || strcmp(s, "True") == 0) {
        set_bool(n, 1);
        return;
    }
    if (strcmp(s, "false") == 0 || strcmp(s, "False") == 0) {
        set_bool(n, 0);
        return;
    }
    if ((s[0] == '"' || s[0] == '\'') && strlen(s) >= 2) {
        char q = s[0];
        size_t len = strlen(s);
        if (s[len - 1] == q) {
            char buf[FL_YAML_STR_LEN];
            size_t ncopy = len - 2;
            if (ncopy >= sizeof buf) ncopy = sizeof buf - 1;
            memcpy(buf, s + 1, ncopy);
            buf[ncopy] = 0;
            set_str(n, buf);
            return;
        }
    }
    if (looks_number(s)) {
        set_num(n, atof(s));
        return;
    }
    set_str(n, s);
}

/* Forward decls for flow parsers. */
static FlYNode *parse_flow_value(FlYDoc *doc, const char **pp, char *err, int err_cap);

static FlYNode *parse_flow_map(FlYDoc *doc, const char **pp, char *err, int err_cap) {
    FlYNode *map = alloc_node(doc);
    if (!map) {
        set_err(err, err_cap, "yaml: too many nodes");
        return NULL;
    }
    map->kind = FL_Y_MAP;
    const char *p = skip_ws(*pp);
    if (*p != '{') {
        set_err(err, err_cap, "yaml: expected '{'");
        return NULL;
    }
    p++;
    FlYNode *tail = NULL;
    for (;;) {
        p = skip_ws(p);
        if (*p == '}') {
            p++;
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        /* key */
        char keybuf[FL_YAML_KEY_LEN];
        int ki = 0;
        while (*p && *p != ':' && *p != '}' && *p != ',' && !isspace((unsigned char)*p)) {
            if (ki + 1 < (int)sizeof keybuf) keybuf[ki++] = *p;
            p++;
        }
        keybuf[ki] = 0;
        p = skip_ws(p);
        if (*p != ':') {
            set_err(err, err_cap, "yaml: flow map expected ':'");
            return NULL;
        }
        p++;
        FlYNode *val = parse_flow_value(doc, &p, err, err_cap);
        if (!val) return NULL;
        snprintf(val->key, sizeof val->key, "%s", keybuf);
        if (!map->child) map->child = val;
        else tail->next = val;
        tail = val;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }
        set_err(err, err_cap, "yaml: flow map syntax");
        return NULL;
    }
    *pp = p;
    return map;
}

static FlYNode *parse_flow_seq(FlYDoc *doc, const char **pp, char *err, int err_cap) {
    FlYNode *seq = alloc_node(doc);
    if (!seq) {
        set_err(err, err_cap, "yaml: too many nodes");
        return NULL;
    }
    seq->kind = FL_Y_SEQ;
    const char *p = skip_ws(*pp);
    if (*p != '[') {
        set_err(err, err_cap, "yaml: expected '['");
        return NULL;
    }
    p++;
    FlYNode *tail = NULL;
    for (;;) {
        p = skip_ws(p);
        if (*p == ']') {
            p++;
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        FlYNode *item = parse_flow_value(doc, &p, err, err_cap);
        if (!item) return NULL;
        if (!seq->child) seq->child = item;
        else tail->next = item;
        tail = item;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            break;
        }
        set_err(err, err_cap, "yaml: flow seq syntax");
        return NULL;
    }
    *pp = p;
    return seq;
}

static FlYNode *parse_flow_value(FlYDoc *doc, const char **pp, char *err, int err_cap) {
    const char *p = skip_ws(*pp);
    if (*p == '{') return parse_flow_map(doc, pp, err, err_cap);
    if (*p == '[') return parse_flow_seq(doc, pp, err, err_cap);
    /* scalar until , } ] */
    char buf[FL_YAML_STR_LEN];
    int bi = 0;
    int in_sq = 0, in_dq = 0;
    while (*p) {
        if (!in_sq && !in_dq && (*p == ',' || *p == '}' || *p == ']')) break;
        if (*p == '\'' && !in_dq) in_sq = !in_sq;
        else if (*p == '"' && !in_sq) in_dq = !in_dq;
        if (bi + 1 < (int)sizeof buf) buf[bi++] = *p;
        p++;
    }
    buf[bi] = 0;
    rtrim_buf(buf);
    FlYNode *n = alloc_node(doc);
    if (!n) {
        set_err(err, err_cap, "yaml: too many nodes");
        return NULL;
    }
    parse_scalar(n, buf);
    *pp = p;
    return n;
}

static FlYNode *parse_value_text(FlYDoc *doc, const char *text, char *err, int err_cap) {
    const char *p = skip_ws(text);
    if (*p == '{' || *p == '[') {
        FlYNode *n = parse_flow_value(doc, &p, err, err_cap);
        if (!n) return NULL;
        p = skip_ws(p);
        if (*p) {
            set_err(err, err_cap, "yaml: trailing junk after flow value");
            return NULL;
        }
        return n;
    }
    FlYNode *n = alloc_node(doc);
    if (!n) {
        set_err(err, err_cap, "yaml: too many nodes");
        return NULL;
    }
    parse_scalar(n, p);
    return n;
}

typedef struct {
    FlYNode *node;
    int indent;
} Frame;

int fl_yaml_parse(FlYDoc *doc, const char *text, char *err, int err_cap) {
    if (!doc || !text) {
        set_err(err, err_cap, "yaml: null args");
        return -1;
    }
    memset(doc, 0, sizeof *doc);
    FlYNode *root = alloc_node(doc);
    if (!root) {
        set_err(err, err_cap, "yaml: oom");
        return -1;
    }
    root->kind = FL_Y_MAP;
    doc->root = root;

    char *buf = strdup(text);
    if (!buf) {
        set_err(err, err_cap, "yaml: oom");
        return -1;
    }

    Frame stack[64];
    int sp = 0;
    stack[0].node = root;
    stack[0].indent = -1;

    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int line_no = 0;
    FlYNode *pending_parent = NULL; /* for key: with nested block */
    int pending_indent = -1;
    char pending_key[FL_YAML_KEY_LEN];
    pending_key[0] = 0;
    int fold_mode = 0;
    FlYNode *fold_node = NULL;
    int fold_indent = 0;

    while (line) {
        line_no++;
        char *raw = line;
        /* strip CR */
        size_t L = strlen(raw);
        if (L && raw[L - 1] == '\r') raw[L - 1] = 0;

        int ind = indent_of(raw);
        if (ind < 0) {
            snprintf(err, err_cap, "yaml line %d: tabs not supported", line_no);
            free(buf);
            return -1;
        }
        char *t = raw + ind;
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }

        if (fold_mode && fold_node) {
            if (ind > fold_indent && *t) {
                size_t cur = strlen(fold_node->str);
                if (cur && cur + 1 < sizeof fold_node->str) {
                    fold_node->str[cur++] = ' ';
                    fold_node->str[cur] = 0;
                }
                snprintf(fold_node->str + strlen(fold_node->str),
                         sizeof fold_node->str - strlen(fold_node->str), "%s", t);
                line = strtok_r(NULL, "\n", &save);
                continue;
            }
            fold_mode = 0;
            fold_node = NULL;
        }

        /* pop frames to parent with smaller indent */
        while (sp > 0 && ind <= stack[sp].indent) sp--;

        char work[1024];
        snprintf(work, sizeof work, "%s", t);
        strip_inline_comment(work);
        if (!*work) {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }

        /* sequence item */
        if (work[0] == '-' && (work[1] == ' ' || work[1] == '\0')) {
            const char *rest = skip_ws(work + 1);
            FlYNode *parent = stack[sp].node;
            if (parent->kind != FL_Y_SEQ) {
                /* convert pending empty map entry into seq, or error */
                if (pending_parent && pending_key[0]) {
                    FlYNode *seq = alloc_node(doc);
                    if (!seq) {
                        set_err(err, err_cap, "yaml: too many nodes");
                        free(buf);
                        return -1;
                    }
                    seq->kind = FL_Y_SEQ;
                    snprintf(seq->key, sizeof seq->key, "%s", pending_key);
                    /* attach to pending_parent map */
                    FlYNode *tail = pending_parent->child;
                    if (!tail) pending_parent->child = seq;
                    else {
                        while (tail->next) tail = tail->next;
                        tail->next = seq;
                    }
                    if (sp + 1 >= (int)(sizeof stack / sizeof stack[0])) {
                        set_err(err, err_cap, "yaml: nest too deep");
                        free(buf);
                        return -1;
                    }
                    sp++;
                    stack[sp].node = seq;
                    stack[sp].indent = pending_indent;
                    pending_key[0] = 0;
                    pending_parent = NULL;
                    parent = seq;
                } else if (parent->kind != FL_Y_SEQ) {
                    snprintf(err, err_cap, "yaml line %d: sequence item outside list",
                             line_no);
                    free(buf);
                    return -1;
                }
            }
            FlYNode *item = NULL;
            if (!*rest) {
                /* bare "-" → empty map for nested keys */
                item = alloc_node(doc);
                if (!item) {
                    set_err(err, err_cap, "yaml: too many nodes");
                    free(buf);
                    return -1;
                }
                item->kind = FL_Y_MAP;
                if (!parent->child) parent->child = item;
                else {
                    FlYNode *tail = parent->child;
                    while (tail->next) tail = tail->next;
                    tail->next = item;
                }
                if (sp + 1 >= (int)(sizeof stack / sizeof stack[0])) {
                    set_err(err, err_cap, "yaml: nest too deep");
                    free(buf);
                    return -1;
                }
                sp++;
                stack[sp].node = item;
                stack[sp].indent = ind;
            } else {
                /* "- key: value" or "- value" */
                const char *colon = NULL;
                int in_sq = 0, in_dq = 0, in_flow = 0;
                for (const char *q = rest; *q; q++) {
                    if (*q == '\'' && !in_dq) in_sq = !in_sq;
                    else if (*q == '"' && !in_sq) in_dq = !in_dq;
                    else if (!in_sq && !in_dq) {
                        if (*q == '{' || *q == '[') in_flow++;
                        else if (*q == '}' || *q == ']') {
                            if (in_flow) in_flow--;
                        } else if (*q == ':' && in_flow == 0 &&
                                   (q[1] == ' ' || q[1] == '\0' || q[1] == '\t')) {
                            colon = q;
                            break;
                        }
                    }
                }
                if (colon) {
                    char kbuf[FL_YAML_KEY_LEN];
                    size_t kn = (size_t)(colon - rest);
                    if (kn >= sizeof kbuf) kn = sizeof kbuf - 1;
                    memcpy(kbuf, rest, kn);
                    kbuf[kn] = 0;
                    rtrim_buf(kbuf);
                    const char *vtext = skip_ws(colon + 1);
                    FlYNode *map = alloc_node(doc);
                    if (!map) {
                        set_err(err, err_cap, "yaml: too many nodes");
                        free(buf);
                        return -1;
                    }
                    map->kind = FL_Y_MAP;
                    if (!parent->child) parent->child = map;
                    else {
                        FlYNode *tail = parent->child;
                        while (tail->next) tail = tail->next;
                        tail->next = map;
                    }
                    if (sp + 1 >= (int)(sizeof stack / sizeof stack[0])) {
                        set_err(err, err_cap, "yaml: nest too deep");
                        free(buf);
                        return -1;
                    }
                    sp++;
                    stack[sp].node = map;
                    stack[sp].indent = ind;

                    if (!*vtext) {
                        snprintf(pending_key, sizeof pending_key, "%s", kbuf);
                        pending_parent = map;
                        pending_indent = ind;
                    } else if (strcmp(vtext, ">") == 0) {
                        FlYNode *vn = alloc_node(doc);
                        if (!vn) {
                            set_err(err, err_cap, "yaml: too many nodes");
                            free(buf);
                            return -1;
                        }
                        set_str(vn, "");
                        snprintf(vn->key, sizeof vn->key, "%s", kbuf);
                        map->child = vn;
                        fold_mode = 1;
                        fold_node = vn;
                        fold_indent = ind;
                    } else {
                        FlYNode *vn = parse_value_text(doc, vtext, err, err_cap);
                        if (!vn) {
                            free(buf);
                            return -1;
                        }
                        snprintf(vn->key, sizeof vn->key, "%s", kbuf);
                        map->child = vn;
                        if (vn->kind == FL_Y_MAP || vn->kind == FL_Y_SEQ) {
                            /* flow already complete */
                        }
                    }
                } else {
                    item = parse_value_text(doc, rest, err, err_cap);
                    if (!item) {
                        free(buf);
                        return -1;
                    }
                    if (!parent->child) parent->child = item;
                    else {
                        FlYNode *tail = parent->child;
                        while (tail->next) tail = tail->next;
                        tail->next = item;
                    }
                }
            }
            line = strtok_r(NULL, "\n", &save);
            continue;
        }

        /* mapping key: value */
        {
            char *colon = NULL;
            int in_sq = 0, in_dq = 0, in_flow = 0;
            for (char *q = work; *q; q++) {
                if (*q == '\'' && !in_dq) in_sq = !in_sq;
                else if (*q == '"' && !in_sq) in_dq = !in_dq;
                else if (!in_sq && !in_dq) {
                    if (*q == '{' || *q == '[') in_flow++;
                    else if (*q == '}' || *q == ']') {
                        if (in_flow) in_flow--;
                    } else if (*q == ':' && in_flow == 0 &&
                               (q[1] == ' ' || q[1] == '\0' || q[1] == '\t')) {
                        colon = q;
                        break;
                    }
                }
            }
            if (!colon) {
                snprintf(err, err_cap, "yaml line %d: expected key:", line_no);
                free(buf);
                return -1;
            }
            *colon = 0;
            rtrim_buf(work);
            const char *vtext = skip_ws(colon + 1);

            FlYNode *parent = stack[sp].node;
            /* If we have a pending key with no value and this is nested deeper,
             * create empty map/seq container. */
            if (pending_parent && pending_key[0] && ind > pending_indent) {
                /* first nested line under pending key — decide map vs seq later;
                 * this is a map key, so create MAP */
                FlYNode *container = alloc_node(doc);
                if (!container) {
                    set_err(err, err_cap, "yaml: too many nodes");
                    free(buf);
                    return -1;
                }
                container->kind = FL_Y_MAP;
                snprintf(container->key, sizeof container->key, "%s", pending_key);
                FlYNode *tail = pending_parent->child;
                if (!tail) pending_parent->child = container;
                else {
                    while (tail->next) tail = tail->next;
                    tail->next = container;
                }
                if (sp + 1 >= (int)(sizeof stack / sizeof stack[0])) {
                    set_err(err, err_cap, "yaml: nest too deep");
                    free(buf);
                    return -1;
                }
                sp++;
                stack[sp].node = container;
                stack[sp].indent = pending_indent;
                pending_key[0] = 0;
                pending_parent = NULL;
                parent = container;
            }

            if (parent->kind != FL_Y_MAP) {
                snprintf(err, err_cap, "yaml line %d: map key under non-map", line_no);
                free(buf);
                return -1;
            }

            if (!*vtext) {
                snprintf(pending_key, sizeof pending_key, "%s", work);
                pending_parent = parent;
                pending_indent = ind;
            } else if (strcmp(vtext, ">") == 0) {
                FlYNode *vn = alloc_node(doc);
                if (!vn) {
                    set_err(err, err_cap, "yaml: too many nodes");
                    free(buf);
                    return -1;
                }
                set_str(vn, "");
                snprintf(vn->key, sizeof vn->key, "%s", work);
                if (!parent->child) parent->child = vn;
                else {
                    FlYNode *tail = parent->child;
                    while (tail->next) tail = tail->next;
                    tail->next = vn;
                }
                fold_mode = 1;
                fold_node = vn;
                fold_indent = ind;
            } else {
                FlYNode *vn = parse_value_text(doc, vtext, err, err_cap);
                if (!vn) {
                    free(buf);
                    return -1;
                }
                snprintf(vn->key, sizeof vn->key, "%s", work);
                if (!parent->child) parent->child = vn;
                else {
                    FlYNode *tail = parent->child;
                    while (tail->next) tail = tail->next;
                    tail->next = vn;
                }
            }
        }

        line = strtok_r(NULL, "\n", &save);
    }

    /* dangling pending key → null value */
    if (pending_parent && pending_key[0]) {
        FlYNode *vn = alloc_node(doc);
        if (vn) {
            set_null(vn);
            snprintf(vn->key, sizeof vn->key, "%s", pending_key);
            FlYNode *tail = pending_parent->child;
            if (!tail) pending_parent->child = vn;
            else {
                while (tail->next) tail = tail->next;
                tail->next = vn;
            }
        }
    }

    free(buf);
    return 0;
}

FlYNode *fl_yaml_map_get(const FlYNode *map, const char *key) {
    if (!map || map->kind != FL_Y_MAP || !key) return NULL;
    for (FlYNode *c = map->child; c; c = c->next)
        if (strcmp(c->key, key) == 0) return c;
    return NULL;
}

const char *fl_yaml_as_cstr(const FlYNode *n) {
    if (!n) return "";
    return n->str;
}

int fl_yaml_as_double(const FlYNode *n, double *out) {
    if (!n || !out) return -1;
    if (n->kind == FL_Y_NUM) {
        *out = n->num;
        return 0;
    }
    if (n->kind == FL_Y_STR && looks_number(n->str)) {
        *out = atof(n->str);
        return 0;
    }
    return -1;
}

int fl_yaml_is_map(const FlYNode *n) { return n && n->kind == FL_Y_MAP; }
int fl_yaml_is_seq(const FlYNode *n) { return n && n->kind == FL_Y_SEQ; }

int fl_yaml_map_count(const FlYNode *map) {
    if (!map || map->kind != FL_Y_MAP) return 0;
    int n = 0;
    for (FlYNode *c = map->child; c; c = c->next) n++;
    return n;
}

/* sqjson: recursive-descent JSON parser. Original code. */
#include "sqjson.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQJ_MAX_DEPTH 64

typedef struct {
    const char *p, *end;
    char *err;
    size_t errlen;
    int failed;
} Parser;

static void fail(Parser *ps, const char *fmt, ...) {
    if (ps->failed) return;
    ps->failed = 1;
    if (ps->err && ps->errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(ps->err, ps->errlen, fmt, ap);
        va_end(ap);
    }
}

static void skip_ws(Parser *ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static SqjValue *node(Parser *ps, SqjType t) {
    SqjValue *v = calloc(1, sizeof *v);
    if (!v) fail(ps, "out of memory");
    else v->type = t;
    return v;
}

static int hex4(const char *s, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v << 4 | (unsigned)d;
    }
    *out = v;
    return 0;
}

static size_t put_utf8(char *o, unsigned cp) {
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | cp >> 6);
        o[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | cp >> 12);
        o[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | cp >> 18);
    o[1] = (char)(0x80 | (cp >> 12 & 0x3F));
    o[2] = (char)(0x80 | (cp >> 6 & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* At an opening quote; returns a malloc'd string (escapes never grow the text). */
static char *parse_string(Parser *ps, size_t *len_out) {
    const char *s = ++ps->p;
    while (ps->p < ps->end && *ps->p != '"') {
        if (*ps->p == '\\') ps->p++;
        else if ((unsigned char)*ps->p < 0x20) {
            fail(ps, "control character in string");
            return NULL;
        }
        ps->p++;
    }
    if (ps->p >= ps->end) {
        fail(ps, "unterminated string");
        return NULL;
    }
    const char *e = ps->p++;
    char *out = malloc((size_t)(e - s) + 1), *o = out;
    if (!out) {
        fail(ps, "out of memory");
        return NULL;
    }
    for (const char *q = s; q < e; q++) {
        if (*q != '\\') {
            *o++ = *q;
            continue;
        }
        q++;
        switch (*q) {
        case '"': *o++ = '"'; break;
        case '\\': *o++ = '\\'; break;
        case '/': *o++ = '/'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'n': *o++ = '\n'; break;
        case 'r': *o++ = '\r'; break;
        case 't': *o++ = '\t'; break;
        case 'u': {
            unsigned cp, lo;
            if (e - q < 5 || hex4(q + 1, &cp)) goto bad;
            q += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (e - q < 7 || q[1] != '\\' || q[2] != 'u' || hex4(q + 3, &lo) || lo < 0xDC00 || lo > 0xDFFF)
                    goto bad;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                q += 6;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                goto bad;
            }
            o += put_utf8(o, cp); /* \uXXXX is 6 bytes, UTF-8 at most 3 (4 for a 12-byte pair) */
            break;
        }
        default: goto bad;
        }
    }
    *o = 0;
    *len_out = (size_t)(o - out);
    return out;
bad:
    free(out);
    fail(ps, "bad escape in string");
    return NULL;
}

static SqjValue *parse_value(Parser *ps, int depth);

static SqjValue *parse_container(Parser *ps, int depth, int object) {
    SqjValue *v = node(ps, object ? SQJ_OBJECT : SQJ_ARRAY), **tail;
    if (!v) return NULL;
    tail = &v->child;
    ps->p++;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == (object ? '}' : ']')) {
        ps->p++;
        return v;
    }
    for (;;) {
        char *key = NULL;
        size_t klen;
        skip_ws(ps);
        if (object) {
            if (ps->p >= ps->end || *ps->p != '"') {
                fail(ps, "expected a key");
                break;
            }
            key = parse_string(ps, &klen);
            if (!key) break;
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') {
                free(key);
                fail(ps, "expected ':'");
                break;
            }
            ps->p++;
        }
        SqjValue *c = parse_value(ps, depth + 1);
        if (!c) {
            free(key);
            break;
        }
        c->key = key;
        *tail = c;
        tail = &c->next;
        v->len++;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == (object ? '}' : ']')) {
            ps->p++;
            return v;
        }
        fail(ps, object ? "expected ',' or '}'" : "expected ',' or ']'");
        break;
    }
    sqj_free(v);
    return NULL;
}

static SqjValue *parse_value(Parser *ps, int depth) {
    if (depth > SQJ_MAX_DEPTH) {
        fail(ps, "nested too deeply");
        return NULL;
    }
    skip_ws(ps);
    if (ps->p >= ps->end) {
        fail(ps, "unexpected end of input");
        return NULL;
    }
    char c = *ps->p;
    if (c == '{' || c == '[') return parse_container(ps, depth, c == '{');
    if (c == '"') {
        size_t n;
        char *s = parse_string(ps, &n);
        if (!s) return NULL;
        SqjValue *v = node(ps, SQJ_STRING);
        if (!v) {
            free(s);
            return NULL;
        }
        v->string = s;
        v->len = n;
        return v;
    }
    static const struct {
        const char *word;
        SqjType type;
    } words[] = {{"null", SQJ_NULL}, {"true", SQJ_TRUE}, {"false", SQJ_FALSE}};
    for (size_t i = 0; i < 3; i++) {
        size_t n = strlen(words[i].word);
        if ((size_t)(ps->end - ps->p) >= n && !memcmp(ps->p, words[i].word, n)) {
            ps->p += n;
            return node(ps, words[i].type);
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char buf[64];
        size_t n = 0;
        while (ps->p < ps->end && n < sizeof buf - 1 && strchr("+-0123456789.eE", *ps->p)) buf[n++] = *ps->p++;
        buf[n] = 0;
        char *endp;
        double d = strtod(buf, &endp);
        if (endp != buf + n) {
            fail(ps, "bad number");
            return NULL;
        }
        SqjValue *v = node(ps, SQJ_NUMBER);
        if (v) v->number = d;
        return v;
    }
    fail(ps, "unexpected character '%c'", c);
    return NULL;
}

SqjValue *sqj_parse(const char *text, size_t len, char *err, size_t errlen) {
    Parser ps = {text, text + len, err, errlen, 0};
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        ps.p += 3;
    SqjValue *v = parse_value(&ps, 0);
    if (v) {
        skip_ws(&ps);
        if (ps.p != ps.end) {
            fail(&ps, "trailing data");
            sqj_free(v);
            return NULL;
        }
    }
    return v;
}

void sqj_free(SqjValue *v) {
    while (v) {
        SqjValue *next = v->next;
        sqj_free(v->child);
        free(v->string);
        free(v->key);
        free(v);
        v = next;
    }
}

const SqjValue *sqj_get(const SqjValue *obj, const char *key) {
    if (!obj || obj->type != SQJ_OBJECT) return NULL;
    for (const SqjValue *c = obj->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}

const char *sqj_str(const SqjValue *obj, const char *key) {
    const SqjValue *v = sqj_get(obj, key);
    return v && v->type == SQJ_STRING ? v->string : NULL;
}

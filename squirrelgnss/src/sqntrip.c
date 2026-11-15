/* sqntrip: NTRIP request builder and caster response parser. Original code. */
#include "sqntrip.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sqnt_request_init(SqntRequest *req) {
    memset(req, 0, sizeof *req);
    req->ntrip_version = 1;
}

size_t sqnt_base64(const unsigned char *in, size_t n, char *out, size_t cap) {
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = 4 * ((n + 2) / 3);
    if (!out || cap < need + 1) return (size_t)-1;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned a = in[i];
        unsigned b = (i + 1 < n) ? in[i + 1] : 0;
        unsigned c = (i + 2 < n) ? in[i + 2] : 0;
        unsigned v = (a << 16) | (b << 8) | c;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? B64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

static int appendf(char *out, size_t cap, size_t *len, const char *fmt, ...) {
    va_list ap;
    if (*len >= cap) return -1;
    va_start(ap, fmt);
    int n = vsnprintf(out + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *len) return -1;
    *len += (size_t)n;
    return 0;
}

int sqnt_build_request(const SqntRequest *req, char *out, size_t cap) {
    if (!req || !out || cap < 16) return -1;
    const char *agent = req->agent[0] ? req->agent : "SquirrelGNSS";
    const char *mount = req->mount;
    /* Sourcetable: empty or "/" -> GET / ; else GET /MOUNT (strip leading /). */
    char path[SQNT_MAX_MOUNT + 2];
    if (!mount[0] || (mount[0] == '/' && !mount[1])) {
        snprintf(path, sizeof path, "/");
    } else if (mount[0] == '/') {
        snprintf(path, sizeof path, "%s", mount);
    } else {
        snprintf(path, sizeof path, "/%s", mount);
    }

    size_t len = 0;
    if (appendf(out, cap, &len, "GET %s HTTP/1.0\r\n", path) < 0) return -1;
    if (appendf(out, cap, &len, "User-Agent: NTRIP %s\r\n", agent) < 0) return -1;
    if (req->ntrip_version >= 2) {
        if (appendf(out, cap, &len, "Ntrip-Version: Ntrip/2.0\r\n") < 0) return -1;
        if (req->host[0] && appendf(out, cap, &len, "Host: %s\r\n", req->host) < 0) return -1;
    }
    if (appendf(out, cap, &len, "Accept: */*\r\n") < 0) return -1;
    if (appendf(out, cap, &len, "Connection: close\r\n") < 0) return -1;

    if (req->user[0]) {
        char raw[SQNT_MAX_USER + SQNT_MAX_PASS + 2];
        char b64[4 * ((sizeof raw + 2) / 3) + 4];
        int raw_n = snprintf(raw, sizeof raw, "%s:%s", req->user, req->password);
        if (raw_n < 0 || (size_t)raw_n >= sizeof raw) return -1;
        if (sqnt_base64((const unsigned char *)raw, (size_t)raw_n, b64, sizeof b64) == (size_t)-1)
            return -1;
        if (appendf(out, cap, &len, "Authorization: Basic %s\r\n", b64) < 0) return -1;
    }
    if (appendf(out, cap, &len, "\r\n") < 0) return -1;
    return (int)len;
}

void sqnt_parser_init(SqntParser *p, unsigned char *body, size_t body_cap) {
    memset(p, 0, sizeof *p);
    p->body = body;
    p->body_cap = body_cap;
}

static void trim_cr(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static int starts_ci(const char *s, const char *prefix) {
    for (; *prefix; s++, prefix++) {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static void classify_kind(SqntParser *p) {
    if (p->status != 200) {
        p->kind = SQNT_KIND_ERROR;
        return;
    }
    if (p->content_type[0] &&
        (strstr(p->content_type, "gnss/sourcetable") || strstr(p->content_type, "text/plain"))) {
        /* text/plain alone is ambiguous; prefer stream unless body says table */
        if (strstr(p->content_type, "gnss/sourcetable")) {
            p->kind = SQNT_KIND_SOURCETABLE;
            return;
        }
    }
    /* Default for 200: stream. Sourcetable may still be detected once body arrives. */
    p->kind = SQNT_KIND_STREAM;
}

static void maybe_promote_sourcetable(SqntParser *p) {
    if (p->kind != SQNT_KIND_STREAM && p->kind != SQNT_KIND_UNKNOWN) return;
    if (p->body_len < 3) return;
    /* Peek at body start for STR; / CAS; / NET; / SOURCETABLE / ENDSOURCETABLE */
    char peek[32];
    size_t n = p->body_len < sizeof peek - 1 ? p->body_len : sizeof peek - 1;
    memcpy(peek, p->body, n);
    peek[n] = 0;
    if (starts_ci(peek, "STR;") || starts_ci(peek, "CAS;") || starts_ci(peek, "NET;") ||
        starts_ci(peek, "SOURCETABLE") || starts_ci(peek, "ENDSOURCETABLE")) {
        p->kind = SQNT_KIND_SOURCETABLE;
    }
}

static int finish_headers(SqntParser *p) {
    p->hdr[p->hdr_len < sizeof p->hdr ? p->hdr_len : sizeof p->hdr - 1] = 0;
    char *line = p->hdr;
    char *next;
    int first = 1;
    for (; line && *line; line = next) {
        next = strstr(line, "\n");
        if (next) {
            *next = 0;
            next++;
        }
        trim_cr(line);
        if (!line[0]) continue;
        if (first) {
            first = 0;
            /* ICY 200 OK | HTTP/1.x 200 OK | SOURCETABLE 200 OK */
            if (starts_ci(line, "ICY ")) {
                p->icy = 1;
                p->status = (int)strtol(line + 4, NULL, 10);
            } else if (starts_ci(line, "HTTP/")) {
                const char *sp = strchr(line, ' ');
                p->status = sp ? (int)strtol(sp + 1, NULL, 10) : 0;
            } else if (starts_ci(line, "SOURCETABLE ")) {
                p->status = (int)strtol(line + 12, NULL, 10);
                p->kind = SQNT_KIND_SOURCETABLE;
            } else {
                p->status = 0;
                p->kind = SQNT_KIND_ERROR;
            }
            continue;
        }
        if (starts_ci(line, "Content-Type:")) {
            const char *v = line + 13;
            while (*v == ' ' || *v == '\t') v++;
            snprintf(p->content_type, sizeof p->content_type, "%s", v);
        }
    }
    if (p->kind == SQNT_KIND_UNKNOWN) classify_kind(p);
    p->header_done = 1;
    return 0;
}

int sqnt_parser_feed(SqntParser *p, const void *data, size_t n) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t i = 0;
    if (!p || (!bytes && n)) return -1;

    if (!p->header_done) {
        while (i < n) {
            if (p->hdr_len + 1 >= sizeof p->hdr) return -1;
            p->hdr[p->hdr_len++] = (char)bytes[i++];
            if (p->hdr_len >= 4) {
                const char *end = p->hdr + p->hdr_len - 4;
                if (end[0] == '\r' && end[1] == '\n' && end[2] == '\r' && end[3] == '\n') {
                    p->hdr_len -= 4;
                    finish_headers(p);
                    break;
                }
                /* tolerate LF-only blank line */
                if (p->hdr_len >= 2 && p->hdr[p->hdr_len - 2] == '\n' && p->hdr[p->hdr_len - 1] == '\n') {
                    p->hdr_len -= 2;
                    finish_headers(p);
                    break;
                }
            }
        }
    }

    if (p->header_done && i < n) {
        size_t left = n - i;
        if (p->body_len + left > p->body_cap) return -1;
        if (p->body && left) memcpy(p->body + p->body_len, bytes + i, left);
        p->body_len += left;
        maybe_promote_sourcetable(p);
    }
    return 0;
}

static void copy_field(char *dst, size_t cap, const char *start, const char *end) {
    size_t n = (size_t)(end - start);
    if (n >= cap) n = cap - 1;
    memcpy(dst, start, n);
    dst[n] = 0;
}

/* Truncating copy; avoids snprintf("%s") which gcc -Werror=format-truncation
 * rejects when the source buffer is larger than the destination. */
static void copy_trunc(char *dst, size_t cap, const char *src) {
    size_t n;
    if (!dst || !cap) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int parse_str_line(const char *line, SqntStrEntry *e) {
    /* STR;mount;id;format;format-details;carrier;nav;network;country;lat;lon;nmea;... */
    memset(e, 0, sizeof *e);
    if (!starts_ci(line, "STR;")) return 0;
    const char *p = line + 4;
    const char *fields[16];
    int nf = 0;
    fields[nf++] = p;
    for (; *p && nf < 16; p++) {
        if (*p == ';') {
            fields[nf++] = p + 1;
        }
    }
    /* field starts are recorded; lengths via next start or EOL */
    char tmp[SQNT_MAX_STR];
    for (int i = 0; i < nf && i < 12; i++) {
        const char *a = fields[i];
        const char *b = (i + 1 < nf) ? fields[i + 1] - 1 : a + strlen(a);
        copy_field(tmp, sizeof tmp, a, b);
        switch (i) {
        case 0:
            copy_trunc(e->mount, sizeof e->mount, tmp);
            break;
        case 1:
            copy_trunc(e->identifier, sizeof e->identifier, tmp);
            break;
        case 2:
            copy_trunc(e->format, sizeof e->format, tmp);
            break;
        case 4:
            e->carrier = (int)strtol(tmp, NULL, 10);
            break;
        case 5:
            copy_trunc(e->nav_system, sizeof e->nav_system, tmp);
            break;
        case 6:
            copy_trunc(e->network, sizeof e->network, tmp);
            break;
        case 7:
            copy_trunc(e->country, sizeof e->country, tmp);
            break;
        case 8:
            e->lat_deg = strtod(tmp, NULL);
            break;
        case 9:
            e->lon_deg = strtod(tmp, NULL);
            break;
        case 10:
            e->nmea = (int)strtol(tmp, NULL, 10);
            break;
        default:
            break;
        }
    }
    return e->mount[0] != 0;
}

int sqnt_parse_sourcetable(const char *text, size_t len, SqntSourcetable *out) {
    if (!text || !out) return -1;
    memset(out, 0, sizeof *out);
    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && text[i] != '\n') i++;
        size_t end = i;
        if (i < len && text[i] == '\n') i++;
        while (end > start && (text[end - 1] == '\r' || text[end - 1] == ' ')) end--;
        if (end <= start) continue;
        char line[512];
        size_t n = end - start;
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, text + start, n);
        line[n] = 0;
        if (starts_ci(line, "ENDSOURCETABLE")) {
            out->complete = 1;
            break;
        }
        if (starts_ci(line, "STR;")) {
            if (out->n >= SQNT_MAX_ENTRIES) return -1;
            if (parse_str_line(line, &out->entry[out->n])) out->n++;
        }
    }
    return out->n;
}

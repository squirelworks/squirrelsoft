/* sqqr_content: QR payload conventions (see sqqr_content.h). Original
 * code, written from the public descriptions of the formats: the Wi-Fi
 * and MECARD key:value; style, vCard 2.1/3.0/4.0 (RFC 6350) lines,
 * mailto: (RFC 6068), tel: (RFC 3966), geo: (RFC 5870). */
#include "sqqr_content.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small string helpers ---- */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int starts_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (lower((unsigned char)*s) != lower((unsigned char)*prefix))
            return 0;
    return 1;
}

static int eq_ci_n(const char *a, size_t n, const char *b)
{
    size_t i = 0;
    for (; i < n && b[i]; i++)
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
    return i == n && b[i] == 0;
}

static int hexv(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Append a field; empty values are skipped; a value that doesn't fit is cut
 * at a UTF-8 character boundary. */
typedef struct {
    SqqrContent *c;
    size_t used;
} Out;

static void add(Out *o, const char *label, const char *v, size_t n)
{
    SqqrContent *c = o->c;
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r' || v[n - 1] == '\n'))
        n--;
    while (n && (*v == ' ' || *v == '\t')) {
        v++;
        n--;
    }
    if (!n || c->nfields >= SQQR_CONTENT_MAX_FIELDS || o->used + 1 >= sizeof c->buf)
        return;
    size_t room = sizeof c->buf - o->used - 1;
    if (n > room) {
        n = room;
        while (n && ((unsigned char)v[n] & 0xC0) == 0x80)
            n--;
    }
    char *dst = c->buf + o->used;
    memcpy(dst, v, n);
    dst[n] = 0;
    c->field[c->nfields].label = label;
    c->field[c->nfields].value = dst;
    c->nfields++;
    o->used += n + 1;
}

static void adds(Out *o, const char *label, const char *v) { add(o, label, v, strlen(v)); }

/* Drop a UTF-8 sequence cut off at the end of out[0..k) */
static size_t utf8_trim(char *out, size_t k)
{
    size_t i = k;
    while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80)
        i--;
    if (i > 0 && ((unsigned char)out[i - 1] & 0x80)) {
        unsigned lead = (unsigned char)out[i - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (k - (i - 1) < need)
            k = i - 1;
    }
    out[k] = 0;
    return k;
}

/* Backslash escapes as in WIFI: and MECARD: (\x is x) */
static size_t unescape_bs(const char *s, size_t n, char *out, size_t cap)
{
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++) {
        if (s[i] == '\\' && i + 1 < n)
            i++;
        out[k++] = s[i];
    }
    return utf8_trim(out, k);
}

/* vCard escapes: \n and \N are line breaks, \, \; \\ are the character */
static size_t unescape_vcard(const char *s, size_t n, char *out, size_t cap)
{
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            out[k++] = (s[i] == 'n' || s[i] == 'N') ? '\n' : s[i];
        } else {
            out[k++] = s[i];
        }
    }
    return utf8_trim(out, k);
}

/* %XX decoding; plus_space turns + into a space (query strings) */
static size_t pct_decode(const char *s, size_t n, char *out, size_t cap, int plus_space)
{
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++) {
        int h1, h2;
        if (s[i] == '%' && i + 2 < n && (h1 = hexv((unsigned char)s[i + 1])) >= 0 &&
            (h2 = hexv((unsigned char)s[i + 2])) >= 0) {
            char ch = (char)(h1 * 16 + h2);
            out[k++] = ch ? ch : ' ';
            i += 2;
        } else {
            out[k++] = plus_space && s[i] == '+' ? ' ' : s[i];
        }
    }
    return utf8_trim(out, k);
}

/* Find the end of a value in KEY:value; lists: the next ; not escaped */
static size_t value_end(const char *s)
{
    size_t i = 0;
    while (s[i] && s[i] != ';') {
        if (s[i] == '\\' && s[i + 1])
            i++;
        i++;
    }
    return i;
}

/* ---- WIFI:T:WPA;S:name;P:secret;H:true;; ---- */

static int parse_wifi(const char *s, Out *o)
{
    char tmp[1024];
    const char *ssid = NULL, *sec = NULL, *pass = NULL, *hidden = NULL, *eap = NULL, *id = NULL,
               *anon = NULL, *ph2 = NULL;
    size_t ssid_n = 0, sec_n = 0, pass_n = 0, hidden_n = 0, eap_n = 0, id_n = 0, anon_n = 0,
           ph2_n = 0;
    while (*s && *s != ';') {
        const char *colon = strchr(s, ':');
        size_t vend = value_end(s);
        if (!colon || (size_t)(colon - s) > vend)
            return 0;
        const char *k = s, *v = colon + 1;
        size_t kn = (size_t)(colon - s), vn = (size_t)(s + vend - v);
#define KEY(name, var)                                                                             \
    if (eq_ci_n(k, kn, name)) {                                                                    \
        var = v;                                                                                   \
        var##_n = vn;                                                                              \
    }
        KEY("S", ssid) else KEY("T", sec) else KEY("P", pass) else KEY("H", hidden) else KEY(
            "E", eap) else KEY("I", id) else KEY("A", anon) else KEY("PH2", ph2)
#undef KEY
        s += vend;
        if (*s == ';')
            s++;
    }
    if (!ssid)
        return 0;
    /* a value wrapped in double quotes loses them */
#define FIELD(label, var)                                                                          \
    if (var) {                                                                                     \
        size_t n = unescape_bs(var, var##_n, tmp, sizeof tmp);                                     \
        if (n >= 2 && tmp[0] == '"' && tmp[n - 1] == '"' && var[var##_n - 2] != '\\')              \
            add(o, label, tmp + 1, n - 2);                                                         \
        else                                                                                       \
            add(o, label, tmp, n);                                                                 \
    }
    FIELD("Network", ssid)
    if (sec && !eq_ci_n(sec, sec_n, "nopass")) {
        FIELD("Security", sec)
    } else {
        adds(o, "Security", "none");
    }
    FIELD("Password", pass)
    if (hidden && eq_ci_n(hidden, hidden_n, "true"))
        adds(o, "Hidden", "yes");
    FIELD("EAP method", eap)
    FIELD("Identity", id)
    FIELD("Anonymous identity", anon)
    FIELD("Phase 2", ph2)
#undef FIELD
    return 1;
}

/* ---- MECARD:N:Doe,John;TEL:123;EMAIL:a@b;; and MATMSG:TO:..;SUB:..;BODY:..;; ---- */

static int parse_kv_card(const char *s, Out *o, int matmsg)
{
    char tmp[2048];
    int any = 0;
    while (*s && *s != ';') {
        const char *colon = strchr(s, ':');
        size_t vend = value_end(s);
        if (!colon || (size_t)(colon - s) > vend)
            return any;
        const char *k = s;
        size_t kn = (size_t)(colon - s);
        size_t n = unescape_bs(colon + 1, (size_t)(s + vend - colon - 1), tmp, sizeof tmp);
        const char *label = NULL;
        if (matmsg) {
            label = eq_ci_n(k, kn, "TO") ? "To" : eq_ci_n(k, kn, "SUB") ? "Subject"
                  : eq_ci_n(k, kn, "BODY") ? "Body" : NULL;
        } else if (eq_ci_n(k, kn, "N")) {
            /* Last,First -> First Last */
            char *comma = memchr(tmp, ',', n);
            if (comma) {
                char name[2048];
                int w = snprintf(name, sizeof name, "%s %.*s", comma + 1, (int)(comma - tmp), tmp);
                add(o, "Name", name, w < 0 ? 0 : (size_t)w < sizeof name ? (size_t)w : sizeof name - 1);
                any = 1;
            } else {
                add(o, "Name", tmp, n);
                any = 1;
            }
        } else {
            label = eq_ci_n(k, kn, "TEL") || eq_ci_n(k, kn, "TEL-AV") ? "Phone"
                  : eq_ci_n(k, kn, "EMAIL") ? "Email" : eq_ci_n(k, kn, "ADR") ? "Address"
                  : eq_ci_n(k, kn, "URL") ? "URL" : eq_ci_n(k, kn, "NOTE") ? "Note"
                  : eq_ci_n(k, kn, "BDAY") ? "Birthday" : eq_ci_n(k, kn, "NICKNAME") ? "Nickname"
                  : eq_ci_n(k, kn, "SOUND") ? "Reading" : eq_ci_n(k, kn, "ORG") ? "Organization"
                  : NULL;
        }
        if (label) {
            add(o, label, tmp, n);
            any = 1;
        }
        s += vend;
        if (*s == ';')
            s++;
    }
    return any;
}

/* ---- vCard ---- */

/* Split a vCard structured value on unescaped ; into parts, unescaped */
static int vcard_parts(const char *v, size_t n, char parts[][512], int max)
{
    int k = 0;
    size_t start = 0;
    for (size_t i = 0; i <= n && k < max; i++) {
        if (i == n || (v[i] == ';' && (i == 0 || v[i - 1] != '\\'))) {
            unescape_vcard(v + start, i - start, parts[k], 512);
            k++;
            start = i + 1;
        }
    }
    return k;
}

static void join_nonempty(char parts[][512], int n, const int *order, int norder, const char *sep,
                          char *out, size_t cap)
{
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < norder; i++) {
        int j = order[i];
        if (j >= n || !parts[j][0])
            continue;
        int w = snprintf(out + k, cap - k, "%s%s", k ? sep : "", parts[j]);
        if (w < 0 || (size_t)w >= cap - k)
            break;
        k += (size_t)w;
    }
}

static int parse_vcard(const char *s, Out *o)
{
    /* unfold: a line break followed by a space or tab continues the line */
    static const size_t CAP = 8192;
    char text[8192];
    size_t k = 0;
    for (size_t i = 0; s[i] && k + 1 < CAP; i++) {
        if (s[i] == '\r' && s[i + 1] == '\n')
            continue;
        if (s[i] == '\n' && (s[i + 1] == ' ' || s[i + 1] == '\t')) {
            i++;
            continue;
        }
        text[k++] = s[i];
    }
    text[k] = 0;

    int has_fn = 0;
    for (const char *l = text; *l;) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l) : strlen(l);
        if (starts_ci(l, "FN:") || starts_ci(l, "FN;"))
            has_fn = 1;
        l += n + (e ? 1 : 0);
    }
    int any = 0;
    char tmp[2048], parts[8][512];
    for (const char *l = text; *l;) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l) : strlen(l);
        const char *colon = memchr(l, ':', n);
        if (colon) {
            const char *name = l;
            size_t nn = (size_t)(colon - l);
            const char *semi = memchr(name, ';', nn);
            if (semi)
                nn = (size_t)(semi - name);
            const char *dot = memchr(name, '.', nn); /* item1.TEL */
            if (dot) {
                nn -= (size_t)(dot + 1 - name);
                name = dot + 1;
            }
            const char *v = colon + 1;
            size_t vn = n - (size_t)(v - l);
            const char *label = NULL;
            if (eq_ci_n(name, nn, "FN")) {
                label = "Name";
            } else if (eq_ci_n(name, nn, "N") && !has_fn) {
                int np = vcard_parts(v, vn, parts, 8);
                static const int ord[] = {3, 1, 2, 0, 4}; /* prefix given additional family suffix */
                join_nonempty(parts, np, ord, 5, " ", tmp, sizeof tmp);
                adds(o, "Name", tmp);
                any = 1;
            } else if (eq_ci_n(name, nn, "TEL")) {
                label = "Phone";
            } else if (eq_ci_n(name, nn, "EMAIL")) {
                label = "Email";
            } else if (eq_ci_n(name, nn, "ORG")) {
                int np = vcard_parts(v, vn, parts, 8);
                static const int ord[] = {0, 1, 2, 3, 4, 5, 6, 7};
                join_nonempty(parts, np, ord, 8, ", ", tmp, sizeof tmp);
                adds(o, "Organization", tmp);
                any = 1;
            } else if (eq_ci_n(name, nn, "TITLE")) {
                label = "Title";
            } else if (eq_ci_n(name, nn, "ADR")) {
                int np = vcard_parts(v, vn, parts, 8);
                static const int ord[] = {0, 1, 2, 3, 4, 5, 6};
                join_nonempty(parts, np, ord, 7, ", ", tmp, sizeof tmp);
                adds(o, "Address", tmp);
                any = 1;
            } else if (eq_ci_n(name, nn, "URL")) {
                label = "URL";
            } else if (eq_ci_n(name, nn, "NOTE")) {
                label = "Note";
            } else if (eq_ci_n(name, nn, "BDAY")) {
                label = "Birthday";
            }
            if (label) {
                size_t m = unescape_vcard(v, vn, tmp, sizeof tmp);
                if (!strcmp(label, "Phone") && starts_ci(tmp, "tel:"))
                    add(o, label, tmp + 4, m - 4);
                else
                    add(o, label, tmp, m);
                any = 1;
            }
        }
        l += n + (e ? 1 : 0);
    }
    return any;
}

/* ---- URIs ---- */

/* mailto:a@b,c@d?subject=..&body=..&cc=.. */
static int parse_mailto(const char *s, Out *o)
{
    char tmp[2048];
    const char *q = strchr(s, '?');
    size_t an = q ? (size_t)(q - s) : strlen(s);
    pct_decode(s, an, tmp, sizeof tmp, 0);
    adds(o, "To", tmp);
    while (q && *q) {
        q++;
        const char *amp = strchr(q, '&');
        size_t n = amp ? (size_t)(amp - q) : strlen(q);
        const char *eq = memchr(q, '=', n);
        if (eq) {
            size_t kn = (size_t)(eq - q);
            const char *label = eq_ci_n(q, kn, "subject") ? "Subject" : eq_ci_n(q, kn, "body") ? "Body"
                              : eq_ci_n(q, kn, "cc") ? "Cc" : eq_ci_n(q, kn, "to") ? "To" : NULL;
            if (label) {
                size_t m = pct_decode(eq + 1, n - kn - 1, tmp, sizeof tmp, 0);
                add(o, label, tmp, m);
            }
        }
        q = amp;
    }
    return 1;
}

static int is_number(const char *s, size_t n, double lo, double hi)
{
    char buf[64], *end;
    if (n == 0 || n >= sizeof buf)
        return 0;
    memcpy(buf, s, n);
    buf[n] = 0;
    double v = strtod(buf, &end);
    return *end == 0 && v >= lo && v <= hi;
}

/* geo:lat,lon[,alt][;params][?q=...] */
static int parse_geo(const char *s, Out *o)
{
    const char *end = s + strcspn(s, ";?");
    const char *c1 = memchr(s, ',', (size_t)(end - s));
    if (!c1)
        return 0;
    const char *c2 = memchr(c1 + 1, ',', (size_t)(end - c1 - 1));
    const char *lon_end = c2 ? c2 : end;
    if (!is_number(s, (size_t)(c1 - s), -90, 90) ||
        !is_number(c1 + 1, (size_t)(lon_end - c1 - 1), -180, 180) ||
        (c2 && !is_number(c2 + 1, (size_t)(end - c2 - 1), -1e6, 1e6)))
        return 0;
    add(o, "Latitude", s, (size_t)(c1 - s));
    add(o, "Longitude", c1 + 1, (size_t)(lon_end - c1 - 1));
    if (c2)
        add(o, "Altitude", c2 + 1, (size_t)(end - c2 - 1));
    const char *q = strstr(end, "q=");
    if (q && (q[-1] == '?' || q[-1] == '&')) {
        char tmp[1024];
        size_t m = pct_decode(q + 2, strcspn(q + 2, "&"), tmp, sizeof tmp, 1);
        add(o, "Query", tmp, m);
    }
    return 1;
}

static int looks_like_url(const char *s)
{
    if (starts_ci(s, "http://") || starts_ci(s, "https://"))
        return s[strcspn(s, " \t\r\n")] == 0 && strlen(s) > 8;
    if (starts_ci(s, "www.")) {
        const char *dot = strchr(s + 4, '.');
        return dot && dot[1] && s[strcspn(s, " \t\r\n")] == 0;
    }
    return 0;
}

void sqqr_content_parse(const char *text, SqqrContent *c)
{
    memset(c, 0, sizeof *c);
    c->type = SQQR_CT_TEXT;
    if (!text)
        return;
    Out o = {c, 0};
    const char *s = text;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    char tmp[2048];
    if (starts_ci(s, "WIFI:")) {
        if (parse_wifi(s + 5, &o))
            c->type = SQQR_CT_WIFI;
    } else if (starts_ci(s, "BEGIN:VCARD")) {
        if (parse_vcard(s, &o))
            c->type = SQQR_CT_CONTACT;
    } else if (starts_ci(s, "MECARD:")) {
        if (parse_kv_card(s + 7, &o, 0))
            c->type = SQQR_CT_CONTACT;
    } else if (starts_ci(s, "MATMSG:")) {
        if (parse_kv_card(s + 7, &o, 1))
            c->type = SQQR_CT_EMAIL;
    } else if (starts_ci(s, "mailto:")) {
        if (parse_mailto(s + 7, &o))
            c->type = SQQR_CT_EMAIL;
    } else if (starts_ci(s, "tel:")) {
        size_t m = pct_decode(s + 4, strcspn(s + 4, ";"), tmp, sizeof tmp, 0);
        add(&o, "Number", tmp, m);
        c->type = SQQR_CT_PHONE;
    } else if (starts_ci(s, "SMSTO:") || starts_ci(s, "MMSTO:")) {
        const char *num = s + 6, *colon = strchr(num, ':');
        add(&o, "Number", num, colon ? (size_t)(colon - num) : strlen(num));
        if (colon)
            adds(&o, "Message", colon + 1);
        c->type = SQQR_CT_SMS;
    } else if (starts_ci(s, "sms:")) {
        const char *num = s + 4, *q = strchr(num, '?');
        add(&o, "Number", num, q ? (size_t)(q - num) : strlen(num));
        const char *b = q ? strstr(q, "body=") : NULL;
        if (b && (b[-1] == '?' || b[-1] == '&')) {
            size_t m = pct_decode(b + 5, strcspn(b + 5, "&"), tmp, sizeof tmp, 0);
            add(&o, "Message", tmp, m);
        }
        c->type = SQQR_CT_SMS;
    } else if (starts_ci(s, "geo:")) {
        if (parse_geo(s + 4, &o))
            c->type = SQQR_CT_GEO;
    } else if (starts_ci(s, "URLTO:")) {
        adds(&o, "URL", s + 6);
        c->type = SQQR_CT_URL;
    } else if (looks_like_url(s)) {
        adds(&o, "URL", s);
        c->type = SQQR_CT_URL;
    }
    if (c->type == SQQR_CT_TEXT) { /* structured text that didn't parse: no half-filled fields */
        c->nfields = 0;
        c->buf[0] = 0;
    }
}

const char *sqqr_content_type_name(SqqrContentType t)
{
    static const char *const n[] = {"Text", "Web link", "Wi-Fi network", "Contact",
                                    "E-mail", "Phone", "SMS", "Location"};
    return (unsigned)t < 8 ? n[t] : "Text";
}

const char *sqqr_content_type_id(SqqrContentType t)
{
    static const char *const n[] = {"text", "url", "wifi", "contact", "email", "phone", "sms", "geo"};
    return (unsigned)t < 8 ? n[t] : "text";
}

const char *sqqr_content_get(const SqqrContent *c, const char *label)
{
    for (int i = 0; i < c->nfields; i++)
        if (!strcmp(c->field[i].label, label))
            return c->field[i].value;
    return NULL;
}

static size_t put_escaped(char *out, size_t cap, size_t k, const char *s)
{
    for (; s && *s; s++) {
        if (strchr("\\;,:\"", *s)) {
            if (k + 1 < cap)
                out[k] = '\\';
            k++;
        }
        if (k + 1 < cap)
            out[k] = *s;
        k++;
    }
    return k;
}

static size_t put_raw(char *out, size_t cap, size_t k, const char *s)
{
    for (; *s; s++, k++)
        if (k + 1 < cap)
            out[k] = *s;
    return k;
}

size_t sqqr_content_wifi(const char *ssid, const char *password, const char *security, int hidden,
                         char *out, size_t cap)
{
    const char *sec = security && *security ? security : password && *password ? "WPA" : "nopass";
    size_t k = put_raw(out, cap, 0, "WIFI:T:");
    k = put_raw(out, cap, k, sec);
    k = put_raw(out, cap, k, ";S:");
    k = put_escaped(out, cap, k, ssid);
    k = put_raw(out, cap, k, ";");
    if (password && *password && strcmp(sec, "nopass") != 0) {
        k = put_raw(out, cap, k, "P:");
        k = put_escaped(out, cap, k, password);
        k = put_raw(out, cap, k, ";");
    }
    if (hidden)
        k = put_raw(out, cap, k, "H:true;");
    k = put_raw(out, cap, k, ";");
    if (cap)
        out[k < cap ? k : cap - 1] = 0;
    return k;
}

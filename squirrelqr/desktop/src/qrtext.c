/* qrtext: QR payload bytes -> UTF-8 text. Original code. */
#include "qrtext.h"

#include <stdio.h>
#include <string.h>

#ifdef QR_HAS_ICONV
#include <errno.h>
#include <iconv.h>
#endif

int qr_utf8_valid(const uint8_t *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = s[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        int more;
        uint32_t cp, min;
        if ((c & 0xE0) == 0xC0) {
            more = 1;
            cp = c & 0x1F;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            more = 2;
            cp = c & 0x0F;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            more = 3;
            cp = c & 0x07;
            min = 0x10000;
        } else {
            return 0;
        }
        if (i + (size_t)more >= len) return 0;
        for (int k = 1; k <= more; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return 0;
            cp = cp << 6 | (s[i + k] & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
        i += (size_t)more + 1;
    }
    return 1;
}

static void latin1(const uint8_t *s, size_t len, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < len && o + 3 <= cap; i++) {
        if (s[i] < 0x80) {
            out[o++] = (char)s[i];
        } else {
            out[o++] = (char)(0xC0 | s[i] >> 6);
            out[o++] = (char)(0x80 | (s[i] & 0x3F));
        }
    }
    if (cap) out[o < cap ? o : cap - 1] = 0;
}

static void copy_utf8(const uint8_t *s, size_t len, char *out, size_t cap) {
    if (!cap) return;
    size_t n = len < cap - 1 ? len : cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

/* Strict Shift JIS -> UTF-8. Returns 0, or -1 if the bytes aren't valid Shift
 * JIS (or there's no converter). */
static int sjis(const uint8_t *s, size_t len, char *out, size_t cap) {
#ifdef QR_HAS_ICONV
    iconv_t cd = iconv_open("UTF-8", "SHIFT_JIS");
    if (cd == (iconv_t)-1) cd = iconv_open("UTF-8", "SJIS");
    if (cd == (iconv_t)-1 || !cap) return -1;
    char *in = (char *)s, *o = out;
    size_t inleft = len, outleft = cap - 1;
    size_t r = iconv(cd, &in, &inleft, &o, &outleft);
    iconv_close(cd);
    if (r == (size_t)-1 || inleft) return -1;
    *o = 0;
    return 0;
#else
    (void)s;
    (void)len;
    (void)out;
    (void)cap;
    return -1;
#endif
}

const char *qr_payload_text(const uint8_t *data, size_t len, int eci, int has_kanji, char *out,
                            size_t cap) {
    if (eci == 1 || eci == 3) {
        latin1(data, len, out, cap);
        return "ISO-8859-1";
    }
    if (eci == 26 && qr_utf8_valid(data, len)) {
        copy_utf8(data, len, out, cap);
        return "UTF-8";
    }
    if ((eci == 20 || has_kanji) && sjis(data, len, out, cap) == 0) return "Shift_JIS";
    if (qr_utf8_valid(data, len)) {
        copy_utf8(data, len, out, cap);
        return "UTF-8";
    }
    if (sjis(data, len, out, cap) == 0) return "Shift_JIS";
    latin1(data, len, out, cap);
    return "ISO-8859-1";
}

size_t qr_json_string(const char *s, char *out, size_t cap) {
    size_t o = 0;
#define PUT(ch)                                                                                    \
    do {                                                                                           \
        if (o + 1 < cap) out[o] = (char)(ch);                                                      \
        o++;                                                                                       \
    } while (0)
    PUT('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned c = *p;
        const char *esc = NULL;
        switch (c) {
        case '"': esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        default: break;
        }
        if (esc) {
            for (; *esc; esc++) PUT(*esc);
        } else if (c < 0x20 || c == 0x7F) {
            char u[8];
            snprintf(u, sizeof u, "\\u%04x", c);
            for (char *q = u; *q; q++) PUT(*q);
        } else {
            PUT(c);
        }
    }
    PUT('"');
#undef PUT
    if (cap) out[o < cap ? o : cap - 1] = 0;
    return o;
}

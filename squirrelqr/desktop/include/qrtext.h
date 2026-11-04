/* qrtext: turn a decoded QR payload into UTF-8 text, using the same charset
 * rules as the Android app (SqQr.text): an ECI wins, Kanji mode means Shift JIS,
 * otherwise valid UTF-8, then valid Shift JIS, then ISO-8859-1. */
#ifndef QRTEXT_H
#define QRTEXT_H

#include <stddef.h>
#include <stdint.h>

/* Writes NUL-terminated UTF-8 into out (cap bytes; 4 * len + 1 is always
 * enough) and returns the charset used: "UTF-8", "Shift_JIS" or "ISO-8859-1".
 * Shift JIS needs iconv; without it such payloads come out as ISO-8859-1. */
const char *qr_payload_text(const uint8_t *data, size_t len, int eci, int has_kanji, char *out,
                            size_t cap);

/* 1 if data[0..len) is well-formed UTF-8 (no overlongs, surrogates or > U+10FFFF). */
int qr_utf8_valid(const uint8_t *data, size_t len);

/* Append s as a JSON string literal (with quotes) to out; returns bytes needed
 * excluding the NUL, like snprintf. */
size_t qr_json_string(const char *s, char *out, size_t cap);

#endif

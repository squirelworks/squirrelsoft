/* PC/SC Part 3 transparent exchange framing for FeliCa. Original code written
 * from the PC/SC 2.01 Part 3 supplement (data object tags 5F46 timer, 95
 * transceive, 97 response, C0 generic error status, 96 response status). */
#include "sqpcsc.h"

#include <string.h>

/* FF C2 00 00 02 81 00: start transparent session */
const uint8_t SQS_PCSC_START_SESSION[7] = {0xFF, 0xC2, 0x00, 0x00, 0x02, 0x81, 0x00};
/* FF C2 00 02 04 8F 02 03 00: switch protocol to FeliCa (03), layer 0 (default) */
const uint8_t SQS_PCSC_SWITCH_FELICA[9] = {0xFF, 0xC2, 0x00, 0x02, 0x04, 0x8F, 0x02, 0x03, 0x00};
/* FF C2 00 00 02 82 00: end transparent session */
const uint8_t SQS_PCSC_END_SESSION[7] = {0xFF, 0xC2, 0x00, 0x00, 0x02, 0x82, 0x00};

int sqs_pcsc_wrap(const uint8_t *frame, size_t len, unsigned timeout_us, uint8_t *apdu, size_t cap) {
    size_t body = (timeout_us ? 7 : 0) + 2 + len;
    if (len < 1 || len > 253 || body > 255 || cap < 5 + body + 1) return -1;
    size_t o = 0;
    apdu[o++] = 0xFF;
    apdu[o++] = 0xC2;
    apdu[o++] = 0x00;
    apdu[o++] = 0x01;
    apdu[o++] = (uint8_t)body;
    if (timeout_us) {
        apdu[o++] = 0x5F;
        apdu[o++] = 0x46;
        apdu[o++] = 0x04;
        for (int i = 0; i < 4; i++) apdu[o++] = (uint8_t)(timeout_us >> (8 * i));
    }
    apdu[o++] = 0x95;
    apdu[o++] = (uint8_t)len;
    memcpy(apdu + o, frame, len);
    o += len;
    apdu[o++] = 0x00; /* Le: up to 256 */
    return (int)o;
}

/* BER-TLV walk over the response data objects (1- or 2-byte tags, short or
 * 81/82 long lengths). */
int sqs_pcsc_unwrap(const uint8_t *resp, size_t len, uint8_t *frame, size_t cap) {
    if (len < 2) return -1;
    uint8_t sw1 = resp[len - 2], sw2 = resp[len - 1];
    size_t end = len - 2, i = 0;
    int got = -1, status_error = 0;
    while (i < end) {
        unsigned tag = resp[i++];
        if ((tag & 0x1F) == 0x1F) { /* two-byte tag */
            if (i >= end) return -1;
            tag = tag << 8 | resp[i++];
        }
        if (i >= end) return -1;
        size_t l = resp[i++];
        if (l == 0x81) {
            if (i >= end) return -1;
            l = resp[i++];
        } else if (l == 0x82) {
            if (i + 1 >= end) return -1;
            l = (size_t)resp[i] << 8 | resp[i + 1];
            i += 2;
        } else if (l > 0x7F) {
            return -1;
        }
        if (l > end - i) return -1;
        const uint8_t *v = resp + i;
        if (tag == 0x97) { /* ICC response */
            if (l > cap) return -1;
            memcpy(frame, v, l);
            got = (int)l;
        } else if (tag == 0xC0 && l >= 3) {
            /* generic error status: 00 90 00 is fine; 01 xx is a warning; a
             * timeout from the card (02 64 01 / 01 64 01) means no answer */
            if (v[1] == 0x64 && v[2] == 0x01) status_error = 2;
            else if (v[0] != 0x00 && !(v[1] == 0x90 && v[2] == 0x00)) status_error = 1;
        }
        i += l;
    }
    if (sw1 != 0x90 && sw1 != 0x61) return got > 0 ? got : (status_error == 2 ? 0 : -1);
    (void)sw2;
    if (got > 0) return got;
    return status_error == 1 ? -1 : 0;
}

/* Port-100 / RC-S380 USB framing and InCommRF helpers. Original code from the
 * publicly documented Sony NFC Port-100 bulk protocol: SOF 00 00 FF, always the
 * extended little-endian length form (FF FF + u16 LE + LCS), payload, DCS, 00.
 * Checksums are two's complement so the running sum is 0 mod 256. */
#include "sqpasori.h"

#include <string.h>

const uint8_t SQS_PASORI_ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

uint8_t sqs_pasori_checksum(const uint8_t *p, size_t n) {
    unsigned sum = 0;
    for (size_t i = 0; i < n; i++) sum += p[i];
    return (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);
}

int sqs_pasori_frame_wrap(const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
    /* header 8 (SOF+marker+len+LCS) + payload + DCS + postamble */
    if (!payload || !out || len < 1 || len > 0xFFFF || cap < 8 + len + 2) return -1;
    size_t o = 0;
    out[o++] = 0x00;
    out[o++] = 0x00;
    out[o++] = 0xFF;
    out[o++] = 0xFF;
    out[o++] = 0xFF;
    out[o++] = (uint8_t)(len & 0xFF);
    out[o++] = (uint8_t)((len >> 8) & 0xFF);
    out[o++] = sqs_pasori_checksum(out + 5, 2); /* LCS over LENL LENH */
    memcpy(out + o, payload, len);
    o += len;
    out[o++] = sqs_pasori_checksum(payload, len);
    out[o++] = 0x00;
    return (int)o;
}

int sqs_pasori_frame_unwrap(const uint8_t *frame, size_t len, uint8_t *payload, size_t cap,
                            size_t *payload_len) {
    if (payload_len) *payload_len = 0;
    if (!frame || len < 6) return -1;
    if (frame[0] != 0x00 || frame[1] != 0x00 || frame[2] != 0xFF) return -1;
    /* ACK: 00 00 FF 00 FF 00 */
    if (frame[3] == 0x00 && frame[4] == 0xFF && frame[5] == 0x00 && len >= 6) return 0;
    /* Error frame: 00 00 FF FF FF 00 */
    if (len >= 6 && frame[3] == 0xFF && frame[4] == 0xFF && frame[5] == 0x00) return -1;
    /* Extended data: 00 00 FF FF FF LENL LENH LCS DATA... DCS 00 */
    if (len < 10 || frame[3] != 0xFF || frame[4] != 0xFF) return -1;
    size_t plen = (size_t)frame[5] | ((size_t)frame[6] << 8);
    if (sqs_pasori_checksum(frame + 5, 2) != frame[7]) return -1;
    if (len < 8 + plen + 2) return -1;
    if (sqs_pasori_checksum(frame + 8, plen) != frame[8 + plen]) return -1;
    if (frame[8 + plen + 1] != 0x00) return -1;
    if (!payload || plen > cap) return -1;
    memcpy(payload, frame + 8, plen);
    if (payload_len) *payload_len = plen;
    return 1;
}

int sqs_pasori_cmd_build(uint8_t cmd, const uint8_t *args, size_t narg, uint8_t *out, size_t cap) {
    if (!out || cap < 2 + narg || (narg && !args)) return -1;
    out[0] = 0xD6;
    out[1] = cmd;
    if (narg) memcpy(out + 2, args, narg);
    return (int)(2 + narg);
}

int sqs_pasori_incomm_body(int timeout_ms, const uint8_t *felica, size_t flen, uint8_t *out,
                           size_t cap) {
    if (!felica || !out || flen < 1 || flen > 253 || cap < 2 + flen) return -1;
    unsigned units = 0;
    if (timeout_ms > 0) {
        /* Port-100 timeout is in 0.1 ms ticks. */
        unsigned long u = (unsigned long)timeout_ms * 10ul;
        if (u > 0xFFFFul) u = 0xFFFFul;
        if (u < 1) u = 1;
        units = (unsigned)u;
    }
    out[0] = (uint8_t)(units & 0xFF);
    out[1] = (uint8_t)((units >> 8) & 0xFF);
    memcpy(out + 2, felica, flen);
    return (int)(2 + flen);
}

int sqs_pasori_incomm_unwrap(const uint8_t *rsp, size_t len, uint8_t *frame, size_t cap) {
    /* Reply body after D7 05: 4-byte status, 1 reserved/length byte, then the
     * FeliCa frame (length byte first). Status 80 xx xx xx (and siblings) mean
     * the card did not answer within the timeout. */
    if (!rsp) return -1;
    if (len < 4) return -1;
    if (rsp[0] != 0x00 || rsp[1] != 0x00 || rsp[2] != 0x00 || rsp[3] != 0x00) {
        /* 0x80...... is the usual timeout / no-response status. */
        if (rsp[0] == 0x80) return 0;
        return -1;
    }
    if (len <= 5) return 0;
    size_t flen = len - 5;
    if (flen > cap || !frame) return -1;
    memcpy(frame, rsp + 5, flen);
    return (int)flen;
}

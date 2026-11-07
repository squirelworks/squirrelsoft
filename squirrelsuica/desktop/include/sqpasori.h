/* sqpasori: original Sony PaSoRi RC-S380 (NFC Port-100) USB driver.
 *
 * The RC-S380 is not a CCID reader, so PC/SC cannot talk to it. This module
 * speaks the public Port-100 USB bulk framing (SOF / extended length /
 * two's-complement checksums) and the chipset initiator commands needed for
 * FeliCa polling and Read Without Encryption. Host payload starts with 0xD6,
 * the device answers with 0xD7 and command+1. Written from published protocol
 * notes and USB traces; no proprietary SDK code.
 *
 * Pure framing helpers are unit-tested without hardware. Live open/exchange
 * needs libusb-1.0; builds without it use a stub that reports unavailable.
 * Owner verifies a real RC-S380 after merge. */
#ifndef SQPASORI_H
#define SQPASORI_H

#include "sqcard.h"

#include <stddef.h>
#include <stdint.h>

/* Sony USB IDs for the RC-S380 / NFC Port-100 family. */
#define SQS_PASORI_VID 0x054Cu
#define SQS_PASORI_PID_S380 0x06C3u
#define SQS_PASORI_PID_S380_S 0x06C1u

/* ACK the chipset sends before every data response. */
extern const uint8_t SQS_PASORI_ACK[6];

/* Two's-complement checksum: the byte that makes (sum + csum) ≡ 0 (mod 256). */
uint8_t sqs_pasori_checksum(const uint8_t *p, size_t n);

/* Wrap a chipset payload (D6 cmd ...) in a Port-100 USB frame.
 * Returns frame length, or -1 if it does not fit. */
int sqs_pasori_frame_wrap(const uint8_t *payload, size_t len, uint8_t *out, size_t cap);

/* Parse one USB frame into *payload_len bytes at payload.
 * Returns 1 for a data frame, 0 for ACK, -1 for error/malformed.
 * On data, payload receives the raw chipset bytes (D7 ...). */
int sqs_pasori_frame_unwrap(const uint8_t *frame, size_t len, uint8_t *payload, size_t cap,
                            size_t *payload_len);

/* Build a host command: D6 | cmd | args. Returns length, or -1. */
int sqs_pasori_cmd_build(uint8_t cmd, const uint8_t *args, size_t narg, uint8_t *out, size_t cap);

/* InCommRF host body: timeout_le_u16 (0.1 ms units) || felica_frame.
 * timeout_ms is converted to Port-100 units (ms * 10, clamped). */
int sqs_pasori_incomm_body(int timeout_ms, const uint8_t *felica, size_t flen, uint8_t *out,
                           size_t cap);

/* Pull a FeliCa frame out of an InCommRF reply (bytes after D7 05).
 * Returns frame length, 0 when the card did not answer, -1 on chipset error. */
int sqs_pasori_incomm_unwrap(const uint8_t *rsp, size_t len, uint8_t *frame, size_t cap);

/* Chipset command codes used by this driver. */
enum {
    SQS_PASORI_CMD_IN_SET_RF = 0x00,
    SQS_PASORI_CMD_IN_SET_PROTOCOL = 0x02,
    SQS_PASORI_CMD_IN_COMM_RF = 0x04,
    SQS_PASORI_CMD_SWITCH_RF = 0x06,
    SQS_PASORI_CMD_GET_FIRMWARE_VERSION = 0x20,
    SQS_PASORI_CMD_GET_PD_DATA_VERSION = 0x22,
    SQS_PASORI_CMD_SET_COMMAND_TYPE = 0x2A
};

/* ---- USB reader access (libusb builds) ---- */

typedef struct SqsPasori SqsPasori;

/* Names as NUL-terminated strings ending with an empty name. Returns count, or
 * -1 with err set. A build without libusb returns 0. */
int sqs_pasori_list(char *buf, size_t cap, char *err, size_t errlen);

/* Open the first matching RC-S380 (reader NULL, or a substring of the list
 * name), initialise initiator mode for 212 kbps FeliCa, and optionally wait
 * up to wait_ms for a card that answers Polling. *no_card is set when the
 * reader opened but no card answered. */
SqsPasori *sqs_pasori_open(const char *reader, int wait_ms, int *no_card, char *err, size_t errlen);
void sqs_pasori_close(SqsPasori *p);
SqsTransport sqs_pasori_transport(SqsPasori *p);
const char *sqs_pasori_reader_name(const SqsPasori *p);

#endif

/* sqpcsc: FeliCa over a PC/SC contactless reader, using the transparent
 * exchange of PC/SC 2.01 Part 3 (pseudo-APDU class FF, INS C2), which Sony's
 * RC-S300 and other CCID readers implement. pcsc-lite on Linux and macOS,
 * winscard on Windows. Read-only use. */
#ifndef SQPCSC_H
#define SQPCSC_H

#include "sqcard.h"

#include <stddef.h>
#include <stdint.h>

/* ---- APDU framing, pure functions (unit-tested without a reader) ---- */

/* Transparent Exchange APDU carrying one FeliCa frame (with its length byte):
 * FF C2 00 01 Lc [5F 46 04 timeout_us(LE)] [95 len frame] 00.
 * Returns the APDU length, or -1 if it doesn't fit. */
int sqs_pcsc_wrap(const uint8_t *frame, size_t len, unsigned timeout_us, uint8_t *apdu, size_t cap);

/* Pull the card's frame out of a Transparent Exchange response: data objects
 * then SW1 SW2. Returns the frame length (0 when the card did not answer, for
 * a timeout or no-response status), or -1 for a reader error or malformed reply. */
int sqs_pcsc_unwrap(const uint8_t *resp, size_t len, uint8_t *frame, size_t cap);

/* Start Transparent Session / switch to FeliCa (212 kbps) / end session APDUs. */
extern const uint8_t SQS_PCSC_START_SESSION[7], SQS_PCSC_SWITCH_FELICA[9], SQS_PCSC_END_SESSION[7];

/* ---- reader access (only in builds with PC/SC) ---- */

typedef struct SqsPcsc SqsPcsc;

/* Readers as a list of NUL-terminated names ending with an empty name, written
 * to buf. Returns the count, or -1 with err set. */
int sqs_pcsc_list(char *buf, size_t cap, char *err, size_t errlen);

/* Wait up to wait_ms for a card on the reader (NULL = first reader that has
 * one), connect and start a FeliCa session. Returns NULL with err set; *no_card
 * is set when nothing was presented in time. */
SqsPcsc *sqs_pcsc_open(const char *reader, int wait_ms, int *no_card, char *err, size_t errlen);
void sqs_pcsc_close(SqsPcsc *p);
SqsTransport sqs_pcsc_transport(SqsPcsc *p);
const char *sqs_pcsc_reader_name(const SqsPcsc *p);

#endif

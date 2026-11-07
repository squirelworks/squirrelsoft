/* sqpasori stub for builds without libusb. Framing still links from
 * pasori_frame.c; only the USB open/list path is unavailable. */
#include "sqpasori.h"

#include <stdio.h>

static int unavailable(char *err, size_t n) {
    if (err && n)
        snprintf(err, n, "this build has no PaSoRi USB support (install libusb-1.0-0-dev and rebuild)");
    return -1;
}

int sqs_pasori_list(char *buf, size_t cap, char *err, size_t n) {
    if (buf && cap >= 2) buf[0] = buf[1] = 0;
    (void)unavailable;
    (void)err;
    (void)n;
    return 0; /* no USB readers in a stub build */
}

SqsPasori *sqs_pasori_open(const char *reader, int wait_ms, int *no_card, char *err, size_t n) {
    (void)reader;
    (void)wait_ms;
    *no_card = 0;
    unavailable(err, n);
    return NULL;
}

void sqs_pasori_close(SqsPasori *p) { (void)p; }

static int none(void *c, const uint8_t *a, size_t b, uint8_t *d, size_t e, int f) {
    (void)c, (void)a, (void)b, (void)d, (void)e, (void)f;
    return -1;
}

SqsTransport sqs_pasori_transport(SqsPasori *p) {
    SqsTransport t = {none, p};
    return t;
}

const char *sqs_pasori_reader_name(const SqsPasori *p) {
    (void)p;
    return "";
}

/* sqpcsc stub for builds without PC/SC headers. */
#include "sqpcsc.h"

#include <stdio.h>

static int unavailable(char *err, size_t n) {
    if (err && n) snprintf(err, n, "this build has no PC/SC support (install libpcsclite-dev and rebuild)");
    return -1;
}

int sqs_pcsc_list(char *buf, size_t cap, char *err, size_t n) {
    (void)buf;
    (void)cap;
    return unavailable(err, n);
}

SqsPcsc *sqs_pcsc_open(const char *reader, int wait_ms, int *no_card, char *err, size_t n) {
    (void)reader;
    (void)wait_ms;
    *no_card = 0;
    unavailable(err, n);
    return NULL;
}

void sqs_pcsc_close(SqsPcsc *p) { (void)p; }

static int none(void *c, const uint8_t *a, size_t b, uint8_t *d, size_t e, int f) {
    (void)c, (void)a, (void)b, (void)d, (void)e, (void)f;
    return -1;
}

SqsTransport sqs_pcsc_transport(SqsPcsc *p) {
    SqsTransport t = {none, p};
    return t;
}

const char *sqs_pcsc_reader_name(const SqsPcsc *p) {
    (void)p;
    return "";
}

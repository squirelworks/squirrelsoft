/*
 * sqntrip: NTRIP client skeleton for SquirrelGNSS (original code).
 *
 * Builds NTRIP/HTTP GET requests and parses caster responses from byte
 * buffers. No sockets and no network: a host (desktop, Android, or a unit
 * test) feeds already-read bytes. That keeps CI offline while the same
 * parser will later sit behind a TCP/TLS transport for RTK corrections
 * (CONTINUANCE: phone GnssMeasurement + NTRIP).
 *
 * Layout follows the public NTRIP 1.0 description (RTCM Paper
 * 200-2004/SC104-STD) and the NTRIP 2.0 HTTP header additions. Response
 * bodies after a stream 200 are opaque correction bytes (typically RTCM);
 * this stub does not decode RTCM.
 */
#ifndef SQNTRIP_H
#define SQNTRIP_H

#include <stddef.h>

#define SQNT_MAX_MOUNT 64
#define SQNT_MAX_STR 128
#define SQNT_MAX_ENTRIES 32
#define SQNT_MAX_AGENT 64
#define SQNT_MAX_USER 64
#define SQNT_MAX_PASS 64
#define SQNT_MAX_HOST 256

/* ---- request ---- */

typedef struct {
    char host[SQNT_MAX_HOST];   /* Host: header (NTRIP 2); empty skips it */
    char mount[SQNT_MAX_MOUNT]; /* "" or "/" = sourcetable; else mountpoint */
    char user[SQNT_MAX_USER];
    char password[SQNT_MAX_PASS];
    char agent[SQNT_MAX_AGENT];  /* product after "NTRIP "; empty -> SquirrelGNSS */
    int ntrip_version;          /* 1 (default) or 2 */
} SqntRequest;

void sqnt_request_init(SqntRequest *req);
/* Build a GET request. Returns byte length written (no trailing NUL required
 * by the wire format; a NUL is stored if cap allows), or -1 on bad args /
 * overflow. */
int sqnt_build_request(const SqntRequest *req, char *out, size_t cap);

/* Base64 encode for Authorization: Basic. Returns length without NUL, or
 * (size_t)-1 if out is too small. */
size_t sqnt_base64(const unsigned char *in, size_t n, char *out, size_t cap);

/* ---- response parser ---- */

enum {
    SQNT_KIND_UNKNOWN = 0,
    SQNT_KIND_STREAM = 1,      /* ICY/HTTP 200 body = correction stream */
    SQNT_KIND_SOURCETABLE = 2, /* caster table body */
    SQNT_KIND_ERROR = 3        /* non-200 or refused */
};

typedef struct {
    int header_done;
    int status; /* 200, 401, 404, ...; 0 until the status line is seen */
    int kind;
    int icy; /* 1 when the status line was ICY ... */
    char content_type[96];
    unsigned char *body;
    size_t body_cap;
    size_t body_len;
    /* header assembly (status + headers until blank line) */
    char hdr[2048];
    size_t hdr_len;
} SqntParser;

void sqnt_parser_init(SqntParser *p, unsigned char *body, size_t body_cap);
/* Feed more response bytes. Returns 0, or -1 if the body or header buffer
 * overflows. After header_done, further bytes append to body. */
int sqnt_parser_feed(SqntParser *p, const void *data, size_t n);

/* ---- sourcetable ---- */

typedef struct {
    char mount[SQNT_MAX_MOUNT];
    char identifier[SQNT_MAX_STR];
    char format[48];
    char nav_system[48];
    char network[48];
    char country[8];
    double lat_deg, lon_deg;
    int nmea;  /* caster asks for NMEA GGA when 1 */
    int carrier; /* 0 none, 1 L1, 2 L1+L2 (as published) */
} SqntStrEntry;

typedef struct {
    SqntStrEntry entry[SQNT_MAX_ENTRIES];
    int n;
    int complete; /* saw ENDSOURCETABLE */
} SqntSourcetable;

/* Parse STR; / ENDSOURCETABLE lines from a sourcetable body (NUL-optional).
 * Returns the number of STR entries kept, or -1 on overflow of the table. */
int sqnt_parse_sourcetable(const char *text, size_t len, SqntSourcetable *out);

#endif /* SQNTRIP_H */

/*
 * sqntrip: NTRIP client for SquirrelGNSS (original code).
 *
 * Request builder and response parser are buffer-fed (no OS calls). Live
 * transport sits behind SqntIo: TCP (and a TLS request flag) for a real
 * caster, or any injected byte stream for offline unit tests. CI never
 * dials a remote caster and never embeds credentials.
 *
 * Layout follows the public NTRIP 1.0 description (RTCM Paper
 * 200-2004/SC104-STD) and the NTRIP 2.0 HTTP header additions. Response
 * bodies after a stream 200 are opaque correction bytes (typically RTCM);
 * this module does not decode RTCM.
 */
#ifndef SQNTRIP_H
#define SQNTRIP_H

#include <stddef.h>
#include <stdint.h>

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

/* ---- transport I/O (injectable) ---- */

/* Byte stream used by the NTRIP client. Tests inject synthetic caster
 * bytes; desktop/Android open a TCP socket and wrap it. */
typedef struct SqntIo {
    void *ctx;
    /* Write n bytes. Return n on success, or <0 on error. */
    long (*write)(void *ctx, const void *buf, size_t n);
    /* Read up to cap. Return >0 bytes, 0 on EOF, or <0 on error. */
    long (*read)(void *ctx, void *buf, size_t cap);
    /* Optional; may be NULL. */
    void (*close)(void *ctx);
} SqntIo;

/* In-memory caster for offline tests: reads come from rx, writes append to tx. */
typedef struct {
    const unsigned char *rx;
    size_t rx_len;
    size_t rx_off;
    unsigned char *tx;
    size_t tx_cap;
    size_t tx_len;
    int closed;
} SqntMemIo;

void sqnt_memio_init(SqntMemIo *m, const void *rx, size_t rx_len, unsigned char *tx, size_t tx_cap);
SqntIo sqnt_memio_io(SqntMemIo *m);

/* Session: build a GET, write it, then feed the parser from SqntIo reads. */
typedef struct {
    SqntIo io;
    SqntParser parser;
    unsigned char *body;
    size_t body_cap;
    int request_sent;
    int eof;
    char err[192];
} SqntClient;

void sqnt_client_init(SqntClient *c, SqntIo io, unsigned char *body, size_t body_cap);
/* Build and write the GET. Returns 0, or -1 (message in c->err). */
int sqnt_client_request(SqntClient *c, const SqntRequest *req);
/* One read into the parser. Returns 0 (progress), 1 (EOF), or -1. */
int sqnt_client_pump(SqntClient *c);
/* Pump until header_done, EOF, or error. Returns 0 if headers complete. */
int sqnt_client_drain_headers(SqntClient *c);
/* Pump until EOF or body_cap would overflow (stops before overflow). */
int sqnt_client_drain_body(SqntClient *c);
void sqnt_client_close(SqntClient *c);

/* ---- live TCP (TLS flag reserved) ---- */

#define SQNT_TCP_CLOSED ((intptr_t)-1)

typedef struct {
    intptr_t sock; /* OS socket handle, or SQNT_TCP_CLOSED */
    int use_tls;   /* requested; handshake fails if TLS unsupported */
} SqntTcp;

/* 1 if a TLS stack is linked; 0 today (no third-party TLS in-tree). */
int sqnt_tls_supported(void);

/* Connect to host:port. use_tls nonzero requires sqnt_tls_supported().
 * No credentials are stored; pass user/pass only in SqntRequest. */
int sqnt_tcp_connect(SqntTcp *t, const char *host, const char *port, int use_tls, char *err,
                     size_t err_cap);
/* Bind 127.0.0.1:0 and report the chosen port (offline local caster tests). */
int sqnt_tcp_listen_loopback(SqntTcp *t, int *out_port, char *err, size_t err_cap);
int sqnt_tcp_accept(SqntTcp *listener, SqntTcp *client, char *err, size_t err_cap);
SqntIo sqnt_tcp_io(SqntTcp *t);
void sqnt_tcp_close(SqntTcp *t);

#endif /* SQNTRIP_H */

/* sqntrip transport: injectable I/O, memory caster, portable TCP.
 * Original code. No remote network in unit tests; TLS reserved (unsupported). */
#ifndef _WIN32
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "sqntrip.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sqnt_sock_t;
#define SQNT_INVALID_SOCK INVALID_SOCKET
static int sqnt_sock_err(void) { return WSAGetLastError(); }
static void sqnt_sock_close(sqnt_sock_t s) {
    if (s != INVALID_SOCKET) closesocket(s);
}
static int sqnt_sock_write(sqnt_sock_t s, const void *buf, size_t n) {
    int r = send(s, (const char *)buf, (int)n, 0);
    return r;
}
static int sqnt_sock_read(sqnt_sock_t s, void *buf, size_t cap) {
    return recv(s, (char *)buf, (int)cap, 0);
}
static int wsa_ready;
static int ensure_wsa(char *err, size_t err_cap) {
    if (wsa_ready) return 0;
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc) {
        snprintf(err, err_cap, "WSAStartup failed (%d)", rc);
        return -1;
    }
    wsa_ready = 1;
    return 0;
}
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int sqnt_sock_t;
#define SQNT_INVALID_SOCK (-1)
static int sqnt_sock_err(void) { return errno; }
static void sqnt_sock_close(sqnt_sock_t s) {
    if (s >= 0) close(s);
}
static int sqnt_sock_write(sqnt_sock_t s, const void *buf, size_t n) {
    for (;;) {
        /* MSG_NOSIGNAL: do not SIGPIPE-kill the process if the caster drops. */
#ifdef MSG_NOSIGNAL
        ssize_t r = send(s, buf, n, MSG_NOSIGNAL);
#else
        ssize_t r = send(s, buf, n, 0);
#endif
        if (r < 0 && errno == EINTR) continue;
        return (int)r;
    }
}
static int sqnt_sock_read(sqnt_sock_t s, void *buf, size_t cap) {
    for (;;) {
        ssize_t r = recv(s, buf, cap, 0);
        if (r < 0 && errno == EINTR) continue;
        return (int)r;
    }
}
static int ensure_wsa(char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    return 0;
}
#endif

/* ---- memory I/O ---- */

void sqnt_memio_init(SqntMemIo *m, const void *rx, size_t rx_len, unsigned char *tx, size_t tx_cap) {
    memset(m, 0, sizeof *m);
    m->rx = (const unsigned char *)rx;
    m->rx_len = rx_len;
    m->tx = tx;
    m->tx_cap = tx_cap;
}

static long mem_write(void *ctx, const void *buf, size_t n) {
    SqntMemIo *m = (SqntMemIo *)ctx;
    if (!m || m->closed) return -1;
    if (m->tx_len + n > m->tx_cap) return -1;
    if (n && m->tx) memcpy(m->tx + m->tx_len, buf, n);
    m->tx_len += n;
    return (long)n;
}

static long mem_read(void *ctx, void *buf, size_t cap) {
    SqntMemIo *m = (SqntMemIo *)ctx;
    if (!m || m->closed) return -1;
    if (m->rx_off >= m->rx_len) return 0;
    size_t n = m->rx_len - m->rx_off;
    if (n > cap) n = cap;
    if (n && buf) memcpy(buf, m->rx + m->rx_off, n);
    m->rx_off += n;
    return (long)n;
}

static void mem_close(void *ctx) {
    SqntMemIo *m = (SqntMemIo *)ctx;
    if (m) m->closed = 1;
}

SqntIo sqnt_memio_io(SqntMemIo *m) {
    SqntIo io = {m, mem_write, mem_read, mem_close};
    return io;
}

/* ---- client session ---- */

void sqnt_client_init(SqntClient *c, SqntIo io, unsigned char *body, size_t body_cap) {
    memset(c, 0, sizeof *c);
    c->io = io;
    c->body = body;
    c->body_cap = body_cap;
    sqnt_parser_init(&c->parser, body, body_cap);
}

int sqnt_client_request(SqntClient *c, const SqntRequest *req) {
    if (!c || !req || !c->io.write) {
        if (c) snprintf(c->err, sizeof c->err, "bad client/request");
        return -1;
    }
    char reqbuf[1024];
    int n = sqnt_build_request(req, reqbuf, sizeof reqbuf);
    if (n < 0) {
        snprintf(c->err, sizeof c->err, "request build failed");
        return -1;
    }
    long w = c->io.write(c->io.ctx, reqbuf, (size_t)n);
    if (w != (long)n) {
        snprintf(c->err, sizeof c->err, "request write failed");
        return -1;
    }
    c->request_sent = 1;
    return 0;
}

int sqnt_client_pump(SqntClient *c) {
    if (!c || !c->io.read) {
        if (c) snprintf(c->err, sizeof c->err, "bad client");
        return -1;
    }
    if (c->eof) return 1;
    unsigned char chunk[512];
    long n = c->io.read(c->io.ctx, chunk, sizeof chunk);
    if (n < 0) {
        snprintf(c->err, sizeof c->err, "read failed");
        return -1;
    }
    if (n == 0) {
        c->eof = 1;
        return 1;
    }
    if (sqnt_parser_feed(&c->parser, chunk, (size_t)n) < 0) {
        snprintf(c->err, sizeof c->err, "parser overflow");
        return -1;
    }
    return 0;
}

int sqnt_client_drain_headers(SqntClient *c) {
    while (!c->parser.header_done) {
        int r = sqnt_client_pump(c);
        if (r < 0) return -1;
        if (r > 0) {
            snprintf(c->err, sizeof c->err, "EOF before response headers");
            return -1;
        }
    }
    return 0;
}

int sqnt_client_drain_body(SqntClient *c) {
    if (sqnt_client_drain_headers(c) < 0) return -1;
    for (;;) {
        if (c->eof) return 0;
        /* Stop before a read that cannot fit, so tests can inspect a full body. */
        if (c->parser.body_len >= c->body_cap) return 0;
        int r = sqnt_client_pump(c);
        if (r < 0) return -1;
        if (r > 0) return 0;
    }
}

void sqnt_client_close(SqntClient *c) {
    if (!c) return;
    if (c->io.close) c->io.close(c->io.ctx);
    c->io.write = NULL;
    c->io.read = NULL;
    c->io.close = NULL;
}

/* ---- TCP ---- */

int sqnt_tls_supported(void) {
    /* No OpenSSL/mbedtls in-tree (CONTINUANCE: do not bolt on third-party TLS). */
    return 0;
}

static sqnt_sock_t as_sock(intptr_t s) { return (sqnt_sock_t)s; }

static intptr_t from_sock(sqnt_sock_t s) {
    if (s == SQNT_INVALID_SOCK) return SQNT_TCP_CLOSED;
    return (intptr_t)s;
}

static void set_nodelay(sqnt_sock_t s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
}

/* Avoid SIGPIPE on a dropped caster: Linux/BSD send(MSG_NOSIGNAL), and
 * SO_NOSIGPIPE where available (macOS / some BSDs). Winsock does not raise
 * SIGPIPE; EPIPE/WSAECONNRESET still surface as send() failure. */
static void set_nosigpipe(sqnt_sock_t s) {
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#else
    (void)s;
#endif
}

static long tcp_write(void *ctx, const void *buf, size_t n) {
    SqntTcp *t = (SqntTcp *)ctx;
    if (!t || t->sock == SQNT_TCP_CLOSED) return -1;
    size_t off = 0;
    while (off < n) {
        int r = sqnt_sock_write(as_sock(t->sock), (const char *)buf + off, n - off);
        if (r <= 0) return -1;
        off += (size_t)r;
    }
    return (long)n;
}

static long tcp_read(void *ctx, void *buf, size_t cap) {
    SqntTcp *t = (SqntTcp *)ctx;
    if (!t || t->sock == SQNT_TCP_CLOSED) return -1;
    int r = sqnt_sock_read(as_sock(t->sock), buf, cap);
    if (r < 0) return -1;
    return (long)r;
}

static void tcp_io_close(void *ctx) {
    SqntTcp *t = (SqntTcp *)ctx;
    if (t) sqnt_tcp_close(t);
}

SqntIo sqnt_tcp_io(SqntTcp *t) {
    SqntIo io = {t, tcp_write, tcp_read, tcp_io_close};
    return io;
}

void sqnt_tcp_close(SqntTcp *t) {
    if (!t || t->sock == SQNT_TCP_CLOSED) return;
    sqnt_sock_close(as_sock(t->sock));
    t->sock = SQNT_TCP_CLOSED;
}

int sqnt_tcp_connect(SqntTcp *t, const char *host, const char *port, int use_tls, char *err,
                     size_t err_cap) {
    if (!t || !host || !port || !port[0]) {
        if (err && err_cap) snprintf(err, err_cap, "bad connect args");
        return -1;
    }
    memset(t, 0, sizeof *t);
    t->sock = SQNT_TCP_CLOSED;
    t->use_tls = use_tls ? 1 : 0;
    if (ensure_wsa(err, err_cap) < 0) return -1;
    if (use_tls && !sqnt_tls_supported()) {
        snprintf(err, err_cap, "TLS not available (no bundled TLS; use plain TCP)");
        return -1;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc) {
        snprintf(err, err_cap, "%s:%s: %s", host, port, gai_strerror(rc));
        return -1;
    }
    sqnt_sock_t fd = SQNT_INVALID_SOCK;
    int last = 0;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == SQNT_INVALID_SOCK) {
            last = sqnt_sock_err();
            continue;
        }
        if (connect(fd, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        last = sqnt_sock_err();
        sqnt_sock_close(fd);
        fd = SQNT_INVALID_SOCK;
    }
    freeaddrinfo(res);
    if (fd == SQNT_INVALID_SOCK) {
#ifdef _WIN32
        snprintf(err, err_cap, "%s:%s: connect failed (%d)", host, port, last);
#else
        snprintf(err, err_cap, "%s:%s: %s", host, port, strerror(last ? last : ECONNREFUSED));
#endif
        return -1;
    }
    set_nodelay(fd);
    set_nosigpipe(fd);
    t->sock = from_sock(fd);
    return 0;
}

int sqnt_tcp_listen_loopback(SqntTcp *t, int *out_port, char *err, size_t err_cap) {
    if (!t || !out_port) {
        if (err && err_cap) snprintf(err, err_cap, "bad listen args");
        return -1;
    }
    memset(t, 0, sizeof *t);
    t->sock = SQNT_TCP_CLOSED;
    if (ensure_wsa(err, err_cap) < 0) return -1;

    sqnt_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SQNT_INVALID_SOCK) {
        snprintf(err, err_cap, "socket failed");
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        sqnt_sock_close(fd);
        snprintf(err, err_cap, "bind 127.0.0.1 failed");
        return -1;
    }
    if (listen(fd, 1) != 0) {
        sqnt_sock_close(fd);
        snprintf(err, err_cap, "listen failed");
        return -1;
    }
#ifdef _WIN32
    int alen = (int)sizeof addr;
#else
    socklen_t alen = sizeof addr;
#endif
    if (getsockname(fd, (struct sockaddr *)&addr, &alen) != 0) {
        sqnt_sock_close(fd);
        snprintf(err, err_cap, "getsockname failed");
        return -1;
    }
    *out_port = (int)ntohs(addr.sin_port);
    t->sock = from_sock(fd);
    return 0;
}

int sqnt_tcp_accept(SqntTcp *listener, SqntTcp *client, char *err, size_t err_cap) {
    if (!listener || !client || listener->sock == SQNT_TCP_CLOSED) {
        if (err && err_cap) snprintf(err, err_cap, "bad accept args");
        return -1;
    }
    memset(client, 0, sizeof *client);
    client->sock = SQNT_TCP_CLOSED;
    sqnt_sock_t fd = accept(as_sock(listener->sock), NULL, NULL);
    if (fd == SQNT_INVALID_SOCK) {
        snprintf(err, err_cap, "accept failed");
        return -1;
    }
    set_nodelay(fd);
    set_nosigpipe(fd);
    client->sock = from_sock(fd);
    return 0;
}

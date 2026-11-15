/* NTRIP client: synthetic HTTP/ICY bytes and local loopback TCP. No remote network. */
#include "sqntrip.h"

#include <stdio.h>
#include <string.h>

static int *nchecks, *nfails;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    (*nchecks)++;
    if (!ok) {
        (*nfails)++;
        fprintf(stderr, "FAIL test_ntrip.c line %d: %s\n", line, what);
    }
}

static void test_base64(void) {
    char out[32];
    CHECK(sqnt_base64((const unsigned char *)"", 0, out, sizeof out) == 0 && out[0] == 0);
    CHECK(sqnt_base64((const unsigned char *)"f", 1, out, sizeof out) == 4 && !strcmp(out, "Zg=="));
    CHECK(sqnt_base64((const unsigned char *)"fo", 2, out, sizeof out) == 4 && !strcmp(out, "Zm8="));
    CHECK(sqnt_base64((const unsigned char *)"foo", 3, out, sizeof out) == 4 && !strcmp(out, "Zm9v"));
    CHECK(sqnt_base64((const unsigned char *)"user:pass", 9, out, sizeof out) == 12 &&
          !strcmp(out, "dXNlcjpwYXNz"));
    CHECK(sqnt_base64((const unsigned char *)"x", 1, out, 4) == (size_t)-1);
}

static void test_request(void) {
    SqntRequest req;
    char buf[512];
    sqnt_request_init(&req);
    snprintf(req.mount, sizeof req.mount, "NEAR");
    int n = sqnt_build_request(&req, buf, sizeof buf);
    CHECK(n > 0 && (size_t)n < sizeof buf);
    CHECK(strstr(buf, "GET /NEAR HTTP/1.0\r\n") == buf);
    CHECK(strstr(buf, "User-Agent: NTRIP SquirrelGNSS\r\n") != NULL);
    CHECK(strstr(buf, "Authorization:") == NULL);
    CHECK(strstr(buf, "\r\n\r\n") != NULL);
    CHECK(strstr(buf, "Ntrip-Version:") == NULL);

    sqnt_request_init(&req);
    n = sqnt_build_request(&req, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "GET / HTTP/1.0\r\n") == buf);

    sqnt_request_init(&req);
    snprintf(req.mount, sizeof req.mount, "/");
    snprintf(req.user, sizeof req.user, "demo");
    snprintf(req.password, sizeof req.password, "none");
    snprintf(req.agent, sizeof req.agent, "SquirrelGNSS/0.1");
    req.ntrip_version = 2;
    snprintf(req.host, sizeof req.host, "caster.example:2101");
    n = sqnt_build_request(&req, buf, sizeof buf);
    CHECK(n > 0);
    CHECK(strstr(buf, "Ntrip-Version: Ntrip/2.0\r\n") != NULL);
    CHECK(strstr(buf, "Host: caster.example:2101\r\n") != NULL);
    CHECK(strstr(buf, "User-Agent: NTRIP SquirrelGNSS/0.1\r\n") != NULL);
    CHECK(strstr(buf, "Authorization: Basic ZGVtbzpub25l\r\n") != NULL);

    CHECK(sqnt_build_request(&req, buf, 8) < 0);
    CHECK(sqnt_build_request(NULL, buf, sizeof buf) < 0);
}

static void test_icy_stream(void) {
    /* Classic NTRIP 1.0 mountpoint reply: ICY 200 then opaque RTCM-like bytes. */
    static const unsigned char reply[] =
        "ICY 200 OK\r\n"
        "Server: SyntheticCaster\r\n"
        "\r\n"
        "\xd3\x00\x13"
        "RTCM-FAKE-PAYLOAD";
    unsigned char body[128];
    SqntParser p;
    sqnt_parser_init(&p, body, sizeof body);
    /* Feed in awkward chunks to exercise the header assembler. */
    CHECK(sqnt_parser_feed(&p, reply, 5) == 0);
    CHECK(!p.header_done);
    CHECK(sqnt_parser_feed(&p, reply + 5, 20) == 0);
    CHECK(sqnt_parser_feed(&p, reply + 25, sizeof reply - 1 - 25) == 0);
    CHECK(p.header_done && p.status == 200 && p.icy == 1);
    CHECK(p.kind == SQNT_KIND_STREAM);
    CHECK(p.body_len == sizeof("\xd3\x00\x13RTCM-FAKE-PAYLOAD") - 1);
    CHECK(body[0] == 0xd3 && body[1] == 0x00 && body[2] == 0x13);
    CHECK(memcmp(body + 3, "RTCM-FAKE-PAYLOAD", 16) == 0);
}

static void test_http_401(void) {
    const char *reply = "HTTP/1.1 401 Unauthorized\r\n"
                        "WWW-Authenticate: Basic realm=\"caster\"\r\n"
                        "\r\n"
                        "auth required\n";
    unsigned char body[64];
    SqntParser p;
    sqnt_parser_init(&p, body, sizeof body);
    CHECK(sqnt_parser_feed(&p, reply, strlen(reply)) == 0);
    CHECK(p.header_done && p.status == 401 && p.kind == SQNT_KIND_ERROR);
    CHECK(p.body_len == strlen("auth required\n"));
}

static void test_sourcetable_response(void) {
    const char *reply =
        "SOURCETABLE 200 OK\r\n"
        "Content-Type: gnss/sourcetable\r\n"
        "\r\n"
        "CAS;caster.example;2101;Synthetic;JPN;0;demo;none;0;NTRIP;none\r\n"
        "STR;NEAR;Tokyo Near;RTCM 3.2;1004(1);2;GPS+GLO;SQNET;JPN;"
        "35.68;139.76;1;0;SquirrelGNSS;none;B;N;9600;\r\n"
        "STR;FAR;Remote Base;RTCM 3.0;1004;1;GPS;SQNET;JPN;34.0;135.0;0;1;"
        "gen;none;N;N;4800;\r\n"
        "ENDSOURCETABLE\r\n";
    unsigned char body[1024];
    SqntParser p;
    sqnt_parser_init(&p, body, sizeof body);
    CHECK(sqnt_parser_feed(&p, reply, strlen(reply)) == 0);
    CHECK(p.header_done && p.status == 200 && p.kind == SQNT_KIND_SOURCETABLE);
    body[p.body_len] = 0;

    SqntSourcetable st;
    CHECK(sqnt_parse_sourcetable((const char *)body, p.body_len, &st) == 2);
    CHECK(st.complete && st.n == 2);
    CHECK(!strcmp(st.entry[0].mount, "NEAR"));
    CHECK(!strcmp(st.entry[0].identifier, "Tokyo Near"));
    CHECK(!strcmp(st.entry[0].format, "RTCM 3.2"));
    CHECK(st.entry[0].carrier == 2);
    CHECK(!strcmp(st.entry[0].nav_system, "GPS+GLO"));
    CHECK(!strcmp(st.entry[0].country, "JPN"));
    CHECK(st.entry[0].lat_deg > 35.67 && st.entry[0].lat_deg < 35.69);
    CHECK(st.entry[0].lon_deg > 139.75 && st.entry[0].lon_deg < 139.77);
    CHECK(st.entry[0].nmea == 1);
    CHECK(!strcmp(st.entry[1].mount, "FAR") && st.entry[1].carrier == 1 && st.entry[1].nmea == 0);
}

static void test_sourcetable_via_http_body(void) {
    /* Some casters say HTTP 200 + text/plain and put STR; in the body. */
    const char *reply = "HTTP/1.0 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "\r\n"
                        "STR;ONLY;Only Mount;RTCM 3.1;;0;GPS;NET;USA;40.0;-74.0;0;0;;;;;\r\n"
                        "ENDSOURCETABLE\r\n";
    unsigned char body[512];
    SqntParser p;
    sqnt_parser_init(&p, body, sizeof body);
    CHECK(sqnt_parser_feed(&p, reply, strlen(reply)) == 0);
    CHECK(p.status == 200);
    CHECK(p.kind == SQNT_KIND_SOURCETABLE);
    SqntSourcetable st;
    CHECK(sqnt_parse_sourcetable((const char *)body, p.body_len, &st) == 1);
    CHECK(st.complete && !strcmp(st.entry[0].mount, "ONLY"));
}

static void test_memio_client_stream(void) {
    static const unsigned char reply[] =
        "ICY 200 OK\r\n"
        "\r\n"
        "\xd3\x00\x08"
        "FAKE-RTCM";
    unsigned char tx[512], body[128];
    SqntMemIo mem;
    SqntClient cli;
    SqntRequest req;

    sqnt_memio_init(&mem, reply, sizeof reply - 1, tx, sizeof tx);
    sqnt_client_init(&cli, sqnt_memio_io(&mem), body, sizeof body);
    sqnt_request_init(&req);
    snprintf(req.mount, sizeof req.mount, "NEAR");
    snprintf(req.user, sizeof req.user, "demo");
    snprintf(req.password, sizeof req.password, "none");

    CHECK(sqnt_client_request(&cli, &req) == 0);
    CHECK(mem.tx_len > 0);
    CHECK(strstr((char *)tx, "GET /NEAR HTTP/1.0\r\n") == (char *)tx);
    CHECK(strstr((char *)tx, "Authorization: Basic ZGVtbzpub25l\r\n") != NULL);

    CHECK(sqnt_client_drain_body(&cli) == 0);
    CHECK(cli.parser.header_done && cli.parser.status == 200 && cli.parser.icy == 1);
    CHECK(cli.parser.kind == SQNT_KIND_STREAM);
    CHECK(cli.parser.body_len == sizeof("\xd3\x00\x08" "FAKE-RTCM") - 1);
    CHECK(body[0] == 0xd3 && memcmp(body + 3, "FAKE-RTCM", 8) == 0);
    sqnt_client_close(&cli);
}

static void test_memio_client_sourcetable(void) {
    const char *reply = "SOURCETABLE 200 OK\r\n"
                        "Content-Type: gnss/sourcetable\r\n"
                        "\r\n"
                        "STR;LOOP;Local;RTCM 3.2;;2;GPS;NET;JPN;35.0;139.0;0;0;;;;;\r\n"
                        "ENDSOURCETABLE\r\n";
    unsigned char tx[256], body[512];
    SqntMemIo mem;
    SqntClient cli;
    SqntRequest req;

    sqnt_memio_init(&mem, reply, strlen(reply), tx, sizeof tx);
    sqnt_client_init(&cli, sqnt_memio_io(&mem), body, sizeof body);
    sqnt_request_init(&req);
    CHECK(sqnt_client_request(&cli, &req) == 0);
    CHECK(strstr((char *)tx, "GET / HTTP/1.0\r\n") == (char *)tx);
    CHECK(sqnt_client_drain_body(&cli) == 0);
    CHECK(cli.parser.kind == SQNT_KIND_SOURCETABLE);
    SqntSourcetable st;
    CHECK(sqnt_parse_sourcetable((const char *)body, cli.parser.body_len, &st) == 1);
    CHECK(st.complete && !strcmp(st.entry[0].mount, "LOOP"));
    sqnt_client_close(&cli);
}

static void test_tls_unsupported(void) {
    CHECK(sqnt_tls_supported() == 0);
    SqntTcp t;
    char err[128];
    CHECK(sqnt_tcp_connect(&t, "127.0.0.1", "9", 1, err, sizeof err) < 0);
    CHECK(strstr(err, "TLS") != NULL);
}

static void test_loopback_tcp_caster(void) {
    /* Local 127.0.0.1 only: synthetic caster, no remote host and no credentials. */
    SqntTcp listener, server, client;
    char err[160];
    int port = 0;
    CHECK(sqnt_tcp_listen_loopback(&listener, &port, err, sizeof err) == 0);
    CHECK(port > 0);

    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    CHECK(sqnt_tcp_connect(&client, "127.0.0.1", portstr, 0, err, sizeof err) == 0);
    CHECK(sqnt_tcp_accept(&listener, &server, err, sizeof err) == 0);

    unsigned char body[256];
    SqntClient cli;
    SqntRequest req;
    sqnt_client_init(&cli, sqnt_tcp_io(&client), body, sizeof body);
    sqnt_request_init(&req);
    snprintf(req.mount, sizeof req.mount, "NEAR");
    req.ntrip_version = 2;
    snprintf(req.host, sizeof req.host, "127.0.0.1:%d", port);
    CHECK(sqnt_client_request(&cli, &req) == 0);

    /* Read the GET on the accepted socket, then reply like a caster. */
    SqntIo rio = sqnt_tcp_io(&server);
    rio.close = NULL; /* keep server open until we finish the reply */
    char got[1024];
    size_t got_len = 0;
    got[0] = 0;
    unsigned char chunk[256];
    while (!strstr(got, "\r\n\r\n")) {
        long n = rio.read(rio.ctx, chunk, sizeof chunk);
        CHECK(n > 0);
        if (got_len + (size_t)n >= sizeof got) break;
        memcpy(got + got_len, chunk, (size_t)n);
        got_len += (size_t)n;
        got[got_len] = 0;
        if (got_len > 800) break;
    }
    CHECK(strstr(got, "GET /NEAR HTTP/1.0\r\n") != NULL);
    CHECK(strstr(got, "Ntrip-Version: Ntrip/2.0\r\n") != NULL);

    static const char reply[] = "ICY 200 OK\r\n\r\n"
                                "\xd3\x00\x04"
                                "LOOP";
    CHECK(rio.write(rio.ctx, reply, sizeof reply - 1) == (long)(sizeof reply - 1));
    sqnt_tcp_close(&server); /* peer EOF after body */

    CHECK(sqnt_client_drain_body(&cli) == 0);
    CHECK(cli.parser.status == 200 && cli.parser.kind == SQNT_KIND_STREAM);
    CHECK(cli.parser.body_len == sizeof("\xd3\x00\x04" "LOOP") - 1);
    CHECK(body[0] == 0xd3 && memcmp(body + 3, "LOOP", 4) == 0);

    sqnt_client_close(&cli); /* closes client TCP */
    sqnt_tcp_close(&listener);
}

void test_ntrip_all(int *checks, int *failures) {
    nchecks = checks;
    nfails = failures;
    test_base64();
    test_request();
    test_icy_stream();
    test_http_401();
    test_sourcetable_response();
    test_sourcetable_via_http_body();
    test_memio_client_stream();
    test_memio_client_sourcetable();
    test_tls_unsupported();
    test_loopback_tcp_caster();
}

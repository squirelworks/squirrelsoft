/* NTRIP client skeleton: synthetic HTTP/ICY caster responses only. No network. */
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

void test_ntrip_all(int *checks, int *failures) {
    nchecks = checks;
    nfails = failures;
    test_base64();
    test_request();
    test_icy_stream();
    test_http_401();
    test_sourcetable_response();
    test_sourcetable_via_http_body();
}

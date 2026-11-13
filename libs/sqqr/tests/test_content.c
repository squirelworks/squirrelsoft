/* sqqr_content: QR payload conventions, on synthetic payloads only. */
#include "sqqr_content.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line)
{
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "test_content.c:%d: CHECK failed: %s\n", line, what);
    }
}

static SqqrContent c;

static int is(const char *label, const char *want)
{
    const char *v = sqqr_content_get(&c, label);
    if (!v || strcmp(v, want) != 0) {
        fprintf(stderr, "  %s = %s%s%s, want \"%s\"\n", label, v ? "\"" : "", v ? v : "(none)",
                v ? "\"" : "", want);
        return 0;
    }
    return 1;
}

static void test_wifi(void)
{
    sqqr_content_parse("WIFI:T:WPA;S:Squirrel Nest;P:acorns\\;and\\:nuts;H:true;;", &c);
    CHECK(c.type == SQQR_CT_WIFI);
    CHECK(is("Network", "Squirrel Nest") && is("Security", "WPA") && is("Password", "acorns;and:nuts") &&
          is("Hidden", "yes"));
    sqqr_content_parse("wifi:S:\"Quoted\";T:nopass;;", &c); /* any order, any case, quotes */
    CHECK(c.type == SQQR_CT_WIFI && is("Network", "Quoted") && is("Security", "none") &&
          !sqqr_content_get(&c, "Password"));
    sqqr_content_parse("WIFI:T:WPA2-EAP;S:corp;E:PEAP;PH2:MSCHAPV2;I:alice;A:anon;P:pw;;", &c);
    CHECK(is("EAP method", "PEAP") && is("Phase 2", "MSCHAPV2") && is("Identity", "alice") &&
          is("Anonymous identity", "anon"));
    sqqr_content_parse("WIFI:T:WPA;P:nossid;;", &c); /* no network name: just text */
    CHECK(c.type == SQQR_CT_TEXT && c.nfields == 0);
    sqqr_content_parse("WIFI:garbage", &c);
    CHECK(c.type == SQQR_CT_TEXT && c.nfields == 0);

    /* build, then read back */
    char w[256];
    size_t n = sqqr_content_wifi("A;B,C", "p\\w\"d", NULL, 1, w, sizeof w);
    CHECK(n == strlen(w) && strcmp(w, "WIFI:T:WPA;S:A\\;B\\,C;P:p\\\\w\\\"d;H:true;;") == 0);
    sqqr_content_parse(w, &c);
    CHECK(c.type == SQQR_CT_WIFI && is("Network", "A;B,C") && is("Password", "p\\w\"d"));
    sqqr_content_wifi("open", "ignored", "nopass", 0, w, sizeof w);
    CHECK(strcmp(w, "WIFI:T:nopass;S:open;;") == 0);
    sqqr_content_wifi("open", NULL, NULL, 0, w, sizeof w);
    CHECK(strcmp(w, "WIFI:T:nopass;S:open;;") == 0);
    char small[10];
    n = sqqr_content_wifi("longer network", "pw", "WPA", 0, small, sizeof small);
    CHECK(n > sizeof small && strlen(small) == sizeof small - 1); /* snprintf-like */
}

static void test_contacts(void)
{
    sqqr_content_parse("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Squirrel;Red;;Dr.;\r\nFN:Dr. Red Squirrel\r\n"
                       "ORG:Acorn Works;R&D\r\nTITLE:Chief Nut Officer\r\nTEL;TYPE=CELL:+81 90 0000 0000\r\n"
                       "item1.EMAIL;TYPE=INTERNET:red@example.org\r\nADR;TYPE=WORK:;;1-2-3 Oak St;Treeville;;100-0001;JP\r\n"
                       "URL:https://example.org/\r\nNOTE:likes\\, acorns\\nand pine nuts\r\n"
                       " (folded)\r\nEND:VCARD\r\n",
                       &c);
    CHECK(c.type == SQQR_CT_CONTACT);
    CHECK(is("Name", "Dr. Red Squirrel") && is("Organization", "Acorn Works, R&D") &&
          is("Title", "Chief Nut Officer") && is("Phone", "+81 90 0000 0000") &&
          is("Email", "red@example.org") && is("Address", "1-2-3 Oak St, Treeville, 100-0001, JP") &&
          is("URL", "https://example.org/") && is("Note", "likes, acorns\nand pine nuts(folded)"));
    int names = 0;
    for (int i = 0; i < c.nfields; i++) names += !strcmp(c.field[i].label, "Name");
    CHECK(names == 1); /* N is ignored when FN is there */

    sqqr_content_parse("BEGIN:VCARD\nVERSION:4.0\nN:Doe;Jane;Q.;Ms.;\nTEL;VALUE=uri:tel:+1-555-0100\nTEL:+1-555-0199\nEND:VCARD", &c);
    CHECK(c.type == SQQR_CT_CONTACT && is("Name", "Ms. Jane Q. Doe") && is("Phone", "+1-555-0100"));
    int phones = 0;
    for (int i = 0; i < c.nfields; i++) phones += !strcmp(c.field[i].label, "Phone");
    CHECK(phones == 2);

    sqqr_content_parse("MECARD:N:Squirrel,Red;SOUND:risu;TEL:0312345678;EMAIL:red@example.org;"
                       "ADR:Treeville\\, JP;NOTE:a\\;b;;",
                       &c);
    CHECK(c.type == SQQR_CT_CONTACT && is("Name", "Red Squirrel") && is("Reading", "risu") &&
          is("Phone", "0312345678") && is("Email", "red@example.org") && is("Address", "Treeville, JP") &&
          is("Note", "a;b"));
    sqqr_content_parse("BEGIN:VCARD\nEND:VCARD", &c);
    CHECK(c.type == SQQR_CT_TEXT);
}

static void test_uris(void)
{
    sqqr_content_parse("mailto:red@example.org,blue@example.org?subject=Hello%20there&body=Nuts%3F+yes&cc=x@example.org", &c);
    CHECK(c.type == SQQR_CT_EMAIL && is("To", "red@example.org,blue@example.org") &&
          is("Subject", "Hello there") && is("Body", "Nuts?+yes") /* RFC 6068: + stays + */ &&
          is("Cc", "x@example.org"));
    sqqr_content_parse("MATMSG:TO:red@example.org;SUB:Hi;BODY:Line\\;two;;", &c);
    CHECK(c.type == SQQR_CT_EMAIL && is("To", "red@example.org") && is("Subject", "Hi") &&
          is("Body", "Line;two"));
    sqqr_content_parse("tel:+81-3-0000-0000", &c);
    CHECK(c.type == SQQR_CT_PHONE && is("Number", "+81-3-0000-0000"));
    sqqr_content_parse("SMSTO:+15550100:See you at the oak", &c);
    CHECK(c.type == SQQR_CT_SMS && is("Number", "+15550100") && is("Message", "See you at the oak"));
    sqqr_content_parse("sms:+15550100?body=Hi%21", &c);
    CHECK(c.type == SQQR_CT_SMS && is("Number", "+15550100") && is("Message", "Hi!"));
    sqqr_content_parse("geo:35.6812,139.7671,40?q=Tokyo+Station", &c);
    CHECK(c.type == SQQR_CT_GEO && is("Latitude", "35.6812") && is("Longitude", "139.7671") &&
          is("Altitude", "40") && is("Query", "Tokyo Station"));
    sqqr_content_parse("geo:95,10", &c); /* latitude out of range */
    CHECK(c.type == SQQR_CT_TEXT && c.nfields == 0);
    sqqr_content_parse("geo:north,east", &c);
    CHECK(c.type == SQQR_CT_TEXT);
    sqqr_content_parse("https://squirrel.works/path?q=1", &c);
    CHECK(c.type == SQQR_CT_URL && is("URL", "https://squirrel.works/path?q=1"));
    sqqr_content_parse("HTTP://EXAMPLE.ORG", &c);
    CHECK(c.type == SQQR_CT_URL);
    sqqr_content_parse("www.example.org", &c);
    CHECK(c.type == SQQR_CT_URL);
    sqqr_content_parse("URLTO:https://example.org", &c);
    CHECK(c.type == SQQR_CT_URL && is("URL", "https://example.org"));
    sqqr_content_parse("http://a b", &c); /* spaces: not a link */
    CHECK(c.type == SQQR_CT_TEXT);
    sqqr_content_parse("just some words", &c);
    CHECK(c.type == SQQR_CT_TEXT && c.nfields == 0);
    sqqr_content_parse("", &c);
    CHECK(c.type == SQQR_CT_TEXT);
    sqqr_content_parse(NULL, &c);
    CHECK(c.type == SQQR_CT_TEXT);
}

static void test_names_and_limits(void)
{
    CHECK(!strcmp(sqqr_content_type_name(SQQR_CT_WIFI), "Wi-Fi network"));
    CHECK(!strcmp(sqqr_content_type_id(SQQR_CT_GEO), "geo"));
    CHECK(!strcmp(sqqr_content_type_id((SqqrContentType)99), "text"));
    /* a huge note is cut at a UTF-8 boundary, never mid-character */
    static char big[20000];
    strcpy(big, "BEGIN:VCARD\nFN:x\nNOTE:");
    size_t k = strlen(big);
    while (k + 4 < sizeof big - 20) {
        memcpy(big + k, "\xE3\x81\x82", 3); /* hiragana a */
        k += 3;
    }
    strcpy(big + k, "\nEND:VCARD");
    sqqr_content_parse(big, &c);
    const char *note = sqqr_content_get(&c, "Note");
    CHECK(c.type == SQQR_CT_CONTACT && note && strlen(note) % 3 == 0 && strlen(note) > 1000);
    /* many fields: no overflow past the field table */
    static char many[8000];
    strcpy(many, "BEGIN:VCARD\n");
    for (int i = 0; i < 60; i++) strcat(many, "TEL:1\n");
    strcat(many, "END:VCARD");
    sqqr_content_parse(many, &c);
    CHECK(c.nfields == SQQR_CONTENT_MAX_FIELDS);
}

int main(void)
{
    test_wifi();
    test_contacts();
    test_uris();
    test_names_and_limits();
    printf("sqqr content: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

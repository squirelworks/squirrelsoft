/* SquirrelSuica desktop: show a FeliCa e-money card's balance and history with
 * station names, using the shared C core (libs/sqfelica). Cards are read live
 * from a PC/SC contactless reader (Sony RC-S300 and other CCID readers), from a
 * PaSoRi RC-S380 over USB (libusb), or replayed from squirrel-felica-dump/1
 * files from SquirrelSuicaAnalysis. */
#include "sqcard.h"
#include "sqgui.h"
#include "sqcsv.h"
#include "sqpasori.h"
#include "sqpcsc.h"
#include "sqfelica.h"
#include "sqjson.h"
#include "sqview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SQS_VERSION
#define SQS_VERSION "dev"
#endif

static void usage(FILE *f) {
    fprintf(f,
            "SquirrelSuica desktop %s: FeliCa balance and history (Suica/PASMO, nanaco, Edy, WAON).\n"
            "\n"
            "usage: squirrelsuica [options]               read a card (PC/SC, or RC-S380 with --usb)\n"
            "       squirrelsuica [options] --dump FILE   read a SquirrelSuicaAnalysis dump (.json)\n"
            "       squirrelsuica --list-readers\n"
            "\n"
            "options:\n"
            "  --json            print JSON instead of text\n"
            "  --gui             show balance + history in an SDL2 window (needs libsdl2-ttf)\n"
            "  --gui-ms MS       like --gui, then auto-quit after MS milliseconds (CI smoke)\n"
            "  --csv             print history as CSV (IDm/PMm omitted by default)\n"
            "  --csv-file PATH   write that CSV to PATH instead of stdout\n"
            "  --include-idm     include IDm/PMm columns in CSV (off by default)\n"
            "  --usb             use a PaSoRi RC-S380 over USB (libusb), not PC/SC\n"
            "  --reader NAME     use the reader whose name contains NAME (default: the first with a card)\n"
            "  --wait SECS       how long to wait for a card (default 10)\n"
            "  --stations CSV    station table (default: data/StationCode.csv next to the program\n"
            "                    or its parent, or $SQUIRRELSUICA_STATIONS)\n"
            "\n"
            "PC/SC readers need pcscd on Linux (sudo apt install pcscd libpcsclite-dev).\n"
            "RC-S380 needs libusb (sudo apt install libusb-1.0-0-dev); real-hardware checks are\n"
            "owner-side after merge. The GUI needs libsdl2-dev + libsdl2-ttf-dev (optional).\n"
            "Read-only: plain FeliCa reads, no keys, no writes.\n"
            "Do not commit real card dumps or CSV exports that contain real IDm.\n",
            SQS_VERSION);
}

static int want_usb_reader(const char *reader) {
    if (!reader || !*reader) return 0;
    return strstr(reader, "usb") || strstr(reader, "USB") || strstr(reader, "RC-S380") ||
           strstr(reader, "PaSoRi") || strstr(reader, "pasori") || strstr(reader, "PASORI");
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    char *buf = malloc(cap + 1);
    while (buf) {
        n += fread(buf + n, 1, cap - n, f);
        if (n < cap) break;
        if (cap > (64u << 20)) {
            free(buf);
            buf = NULL;
            break;
        }
        char *nb = realloc(buf, cap * 2 + 1);
        if (!nb) free(buf);
        buf = nb;
        cap *= 2;
    }
    if (buf && ferror(f)) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[n] = 0;
        *len = n;
    }
    return buf;
}

typedef struct {
    char *csv;
    SqfStation *rows;
    size_t n;
} Stations;

static void load_stations(Stations *st, const char *explicit_path, const char *argv0) {
    char cand[4][1024];
    int nc = 0;
    if (explicit_path) {
        snprintf(cand[nc++], sizeof cand[0], "%s", explicit_path);
    } else {
        const char *env = getenv("SQUIRRELSUICA_STATIONS");
        if (env && *env) snprintf(cand[nc++], sizeof cand[0], "%s", env);
        const char *slash = strrchr(argv0, '/');
#ifdef _WIN32
        const char *bs = strrchr(argv0, '\\');
        if (bs && (!slash || bs > slash)) slash = bs;
#endif
        int dl = slash ? (int)(slash - argv0) : 1;
        const char *dir = slash ? argv0 : ".";
        snprintf(cand[nc++], sizeof cand[0], "%.*s/data/StationCode.csv", dl, dir);
        snprintf(cand[nc++], sizeof cand[0], "%.*s/../data/StationCode.csv", dl, dir);
    }
    for (int i = 0; i < nc; i++) {
        size_t len;
        char *csv = slurp(cand[i], &len);
        if (!csv) continue;
        SqfStation *rows = malloc(16384 * sizeof *rows);
        long n = rows ? sqf_station_index(csv, len, rows, 16384) : -1;
        if (n > 0) {
            st->csv = csv;
            st->rows = rows;
            st->n = (size_t)n;
            return;
        }
        fprintf(stderr, "%s: not a station table\n", cand[i]);
        free(rows);
        free(csv);
    }
    if (explicit_path) fprintf(stderr, "%s: cannot read the station table\n", explicit_path);
}

static const char *station(const Stations *st, const SqfHistory *h, int exit_side) {
    if (!st->n || !(exit_side ? h->has_exit : h->has_enter)) return NULL;
    return exit_side ? sqf_station_resolve(st->rows, st->n, h->region, h->exit_line, h->exit_station, 1)
                     : sqf_station_resolve(st->rows, st->n, h->region, h->enter_line, h->enter_station, 0);
}

static void hex(const uint8_t *b, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

static void json_str(const char *s) {
    if (!s) {
        fputs("null", stdout);
        return;
    }
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 0x20) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static void date_of(const SqfHistory *h, char *out, size_t n) {
    if (h->year) snprintf(out, n, "%04d-%02d-%02d", h->year, h->month, h->day);
    else snprintf(out, n, "?\?\?\?-?\?-?\?");
}

static void code_of(int has, uint8_t line, uint8_t sta, char *out, size_t n) {
    if (has) snprintf(out, n, "%02X/%02X", line, sta);
    else out[0] = 0;
}

static void print_json(const SqsCard *c, const Stations *st) {
    char idm[17], pmm[17];
    hex(c->idm, 8, idm);
    hex(c->pmm, 8, pmm);
    printf("{\"brand\":\"%s\",\"brand_label\":", sqs_brand_id(c->brand));
    json_str(sqs_brand_label(c->brand));
    printf(",\"idm\":\"%s\",\"pmm\":\"%s\",\"system_code\":\"%04x\",\"balance_yen\":", idm, pmm, c->system_code);
    if (c->balance_yen >= 0) printf("%d", (int)c->balance_yen);
    else fputs("null", stdout);
    fputs(",\"history\":[", stdout);
    for (size_t i = 0; i < c->n_history; i++) {
        const SqfHistory *h = &c->history[i];
        char date[16], en[8], ex[8], tm[24];
        date_of(h, date, sizeof date);
        code_of(h->has_enter, h->enter_line, h->enter_station, en, sizeof en);
        code_of(h->has_exit, h->exit_line, h->exit_station, ex, sizeof ex);
        printf("%s{\"date\":\"%s\",\"time\":", i ? "," : "", date);
        if (h->hour >= 0) {
            snprintf(tm, sizeof tm, "%02d:%02d", h->hour, h->minute);
            json_str(tm);
        } else {
            fputs("null", stdout);
        }
        printf(",\"kind\":\"%s\",\"fare_yen\":%d,\"balance_yen\":%d,\"terminal\":%u,\"process\":%u,\"process_label\":",
               sqf_kind_name(h->kind), (int)h->fare_yen, (int)h->balance_yen, h->terminal, h->process);
        json_str(c->brand == SQS_SUICA ? sqf_suica_process_label(h->process) : NULL);
        fputs(",\"enter_code\":", stdout);
        json_str(en[0] ? en : NULL);
        fputs(",\"exit_code\":", stdout);
        json_str(ex[0] ? ex : NULL);
        /* like felica_parse: the station name, or the code when the table doesn't know it */
        const char *ens = station(st, h, 0), *exs = station(st, h, 1);
        fputs(",\"enter_station\":", stdout);
        json_str(ens ? ens : en[0] ? en : NULL);
        fputs(",\"exit_station\":", stdout);
        json_str(exs ? exs : ex[0] ? ex : NULL);
        putchar('}');
    }
    puts("]}");
}

static int read_live_pasori(const char *reader, int wait_s, SqsCard *card) {
    char err[256] = "";
    int no_card = 0;
    if (wait_s > 0) fprintf(stderr, "Hold the card on the RC-S380...\n");
    SqsPasori *p = sqs_pasori_open(reader, wait_s * 1000, &no_card, err, sizeof err);
    if (!p) {
        fprintf(stderr, "%s\n", err);
        return no_card ? 1 : 2;
    }
    SqsTransport t = sqs_pasori_transport(p);
    int rc = sqs_read_card(&t, card);
    sqs_pasori_close(p);
    if (rc != 0) {
        fprintf(stderr, "%s\n",
                rc > 0 ? "the card did not answer FeliCa polling" : "reader error while reading the card");
        return rc > 0 ? 1 : 2;
    }
    return 0;
}

static int read_live_pcsc(const char *reader, int wait_s, SqsCard *card) {
    char err[256] = "";
    int no_card = 0;
    if (wait_s > 0) fprintf(stderr, "Hold the card on the reader...\n");
    SqsPcsc *p = sqs_pcsc_open(reader, wait_s * 1000, &no_card, err, sizeof err);
    if (!p) {
        fprintf(stderr, "%s\n", err);
        return no_card ? 1 : 2;
    }
    SqsTransport t = sqs_pcsc_transport(p);
    int rc = sqs_read_card(&t, card);
    sqs_pcsc_close(p);
    if (rc != 0) {
        fprintf(stderr, "%s\n",
                rc > 0 ? "the card did not answer FeliCa polling" : "reader error while reading the card");
        return rc > 0 ? 1 : 2;
    }
    return 0;
}

static int read_live(const char *reader, int wait_s, int force_usb, SqsCard *card) {
    if (force_usb || want_usb_reader(reader)) return read_live_pasori(reader, wait_s, card);
    return read_live_pcsc(reader, wait_s, card);
}

static int read_dump(const char *path, SqsCard *card) {
    size_t len;
    char *text = slurp(path, &len), err[160] = "";
    if (!text) {
        perror(path);
        return 2;
    }
    SqjValue *doc = sqj_parse(text, len, err, sizeof err);
    free(text);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 2;
    }
    SqsVirtualCard *vc = sqs_virtual_card_new(doc, err, sizeof err);
    sqj_free(doc);
    if (!vc) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 2;
    }
    SqsTransport t = sqs_virtual_transport(vc);
    int rc = sqs_read_card(&t, card);
    sqs_virtual_card_free(vc);
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", path, rc > 0 ? "no card in the dump answered" : "read error");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *dump_path = NULL, *stations_path = NULL, *reader = NULL, *csv_path = NULL;
    int json = 0, csv = 0, include_idm = 0, list = 0, wait_s = 10, force_usb = 0, gui = 0, gui_ms = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            printf("squirrelsuica %s\n", SQS_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--json")) {
            json = 1;
        } else if (!strcmp(argv[i], "--gui")) {
            gui = 1;
        } else if (!strcmp(argv[i], "--gui-ms") && i + 1 < argc) {
            char *end;
            long v = strtol(argv[++i], &end, 10);
            if (*end || v < 1 || v > 600000) {
                fprintf(stderr, "--gui-ms wants milliseconds (1..600000)\n");
                return 2;
            }
            gui = 1;
            gui_ms = (int)v;
        } else if (!strcmp(argv[i], "--csv")) {
            csv = 1;
        } else if (!strcmp(argv[i], "--csv-file") && i + 1 < argc) {
            csv = 1;
            csv_path = argv[++i];
        } else if (!strcmp(argv[i], "--include-idm")) {
            include_idm = 1;
        } else if (!strcmp(argv[i], "--usb")) {
            force_usb = 1;
        } else if (!strcmp(argv[i], "--list-readers")) {
            list = 1;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (!strcmp(argv[i], "--stations") && i + 1 < argc) {
            stations_path = argv[++i];
        } else if (!strcmp(argv[i], "--reader") && i + 1 < argc) {
            reader = argv[++i];
        } else if (!strcmp(argv[i], "--wait") && i + 1 < argc) {
            char *end;
            long v = strtol(argv[++i], &end, 10);
            if (*end || v < 0 || v > 3600) {
                fprintf(stderr, "--wait wants seconds (0..3600)\n");
                return 2;
            }
            wait_s = (int)v;
        } else {
            fprintf(stderr, "unknown argument %s\n\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    if (list) {
        char names[4096], err[256] = "";
        int any = 0;
        int n = sqs_pcsc_list(names, sizeof names, err, sizeof err);
        if (n > 0) {
            for (const char *q = names; *q; q += strlen(q) + 1) {
                puts(q);
                any = 1;
            }
        } else if (n < 0 && force_usb == 0) {
            /* PC/SC missing is fine when a USB PaSoRi is still listed. */
            fprintf(stderr, "PC/SC: %s\n", err);
        }
        err[0] = 0;
        n = sqs_pasori_list(names, sizeof names, err, sizeof err);
        if (n < 0) {
            fprintf(stderr, "%s\n", err);
            return 2;
        }
        for (const char *q = names; n > 0 && *q; q += strlen(q) + 1) {
            puts(q);
            any = 1;
        }
        if (!any) fprintf(stderr, "no card readers\n");
        return 0;
    }
    SqsCard *card = malloc(sizeof *card);
    if (!card) return 2;
    int rc = dump_path ? read_dump(dump_path, card) : read_live(reader, wait_s, force_usb, card);
    if (rc != 0) {
        free(card);
        return rc;
    }
    Stations st = {NULL, NULL, 0};
    load_stations(&st, stations_path, argv[0]);
    SqsView view;
    sqs_view_build(&view, card, st.rows, st.n);
    if (gui) {
        int grc = sqs_gui_show(&view, gui_ms);
        if (grc != 0) {
            free(card);
            free(st.rows);
            free(st.csv);
            /* Stub backend already printed text; treat as success so dump CI still works. */
            return sqs_gui_available() ? 2 : 0;
        }
    } else if (csv) {
        SqsCsvOpts opts = {.include_idm = include_idm};
        FILE *out = stdout;
        if (csv_path) {
            out = fopen(csv_path, "wb");
            if (!out) {
                perror(csv_path);
                free(card);
                free(st.rows);
                free(st.csv);
                return 2;
            }
        }
        int wrc = sqs_csv_write(out, card, st.rows, st.n, &opts);
        if (csv_path) fclose(out);
        if (wrc != 0) {
            fprintf(stderr, "CSV write failed\n");
            free(card);
            free(st.rows);
            free(st.csv);
            return 2;
        }
    } else if (json) {
        print_json(card, &st);
    } else {
        sqs_view_print_text(stdout, &view);
    }
    free(card);
    free(st.rows);
    free(st.csv);
    return 0;
}

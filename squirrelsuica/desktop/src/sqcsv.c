/* sqcsv: card → CSV. Original code. */
#include "sqcsv.h"

#include <stdio.h>
#include <string.h>

static void hex8(const uint8_t *b, char *out) {
    for (size_t i = 0; i < 8; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

static void date_of(const SqfHistory *h, char *out, size_t n) {
    if (h->year) snprintf(out, n, "%04d-%02d-%02d", h->year, h->month, h->day);
    else out[0] = 0;
}

static void code_of(int has, uint8_t line, uint8_t sta, char *out, size_t n) {
    if (has) snprintf(out, n, "%02X/%02X", line, sta);
    else out[0] = 0;
}

static const char *station_name(const SqfStation *stations, size_t n, const SqfHistory *h, int exit_side) {
    if (!stations || !n || !(exit_side ? h->has_exit : h->has_enter)) return NULL;
    return exit_side ? sqf_station_resolve(stations, n, h->region, h->exit_line, h->exit_station, 1)
                     : sqf_station_resolve(stations, n, h->region, h->enter_line, h->enter_station, 0);
}

/* RFC 4180-ish: quote when needed; double internal quotes. */
static int csv_field(FILE *f, const char *s) {
    if (!s) s = "";
    int need = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == ',' || *p == '\n' || *p == '\r') {
            need = 1;
            break;
        }
    }
    if (!need) return fputs(s, f) >= 0 ? 0 : -1;
    if (fputc('"', f) == EOF) return -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"') {
            if (fputs("\"\"", f) < 0) return -1;
        } else if (fputc(*p, f) == EOF) {
            return -1;
        }
    }
    return fputc('"', f) == EOF ? -1 : 0;
}

static int csv_sep(FILE *f) { return fputc(',', f) == EOF ? -1 : 0; }
static int csv_eol(FILE *f) { return fputc('\n', f) == EOF ? -1 : 0; }

static int emit_row(FILE *f, const SqsCard *card, const SqfHistory *h, const SqfStation *stations,
                    size_t n_stations, const SqsCsvOpts *opts, int index) {
    char idm[20] = "", pmm[20] = "", date[16] = "", tm[16] = "", enc[8] = "", exc[8] = "", sys[8];
    snprintf(sys, sizeof sys, "%04x", card->system_code);
    if (opts && opts->include_idm) {
        hex8(card->idm, idm);
        hex8(card->pmm, pmm);
    }
    if (h) {
        date_of(h, date, sizeof date);
        if (h->hour >= 0) snprintf(tm, sizeof tm, "%02d:%02d", h->hour, h->minute);
        code_of(h->has_enter, h->enter_line, h->enter_station, enc, sizeof enc);
        code_of(h->has_exit, h->exit_line, h->exit_station, exc, sizeof exc);
    }
    const char *ens = h ? station_name(stations, n_stations, h, 0) : NULL;
    const char *exs = h ? station_name(stations, n_stations, h, 1) : NULL;
    const char *label = (h && card->brand == SQS_SUICA) ? sqf_suica_process_label(h->process) : NULL;
    char idx[16], fare[24], bal[24], cbal[24];
    if (index > 0) snprintf(idx, sizeof idx, "%d", index);
    else idx[0] = 0;
    if (h) {
        snprintf(fare, sizeof fare, "%d", (int)h->fare_yen);
        snprintf(bal, sizeof bal, "%d", (int)h->balance_yen);
    } else {
        fare[0] = bal[0] = 0;
    }
    if (card->balance_yen >= 0) snprintf(cbal, sizeof cbal, "%d", (int)card->balance_yen);
    else cbal[0] = 0;

    if (csv_field(f, idx)) return -1;
    if (csv_sep(f) || csv_field(f, sqs_brand_id(card->brand))) return -1;
    if (csv_sep(f) || csv_field(f, sys)) return -1;
    if (opts && opts->include_idm) {
        if (csv_sep(f) || csv_field(f, idm)) return -1;
        if (csv_sep(f) || csv_field(f, pmm)) return -1;
    }
    if (csv_sep(f) || csv_field(f, cbal)) return -1;
    if (csv_sep(f) || csv_field(f, date)) return -1;
    if (csv_sep(f) || csv_field(f, tm)) return -1;
    if (csv_sep(f) || csv_field(f, h ? sqf_kind_name(h->kind) : "")) return -1;
    if (csv_sep(f) || csv_field(f, fare)) return -1;
    if (csv_sep(f) || csv_field(f, bal)) return -1;
    if (csv_sep(f) || csv_field(f, ens ? ens : (enc[0] ? enc : ""))) return -1;
    if (csv_sep(f) || csv_field(f, exs ? exs : (exc[0] ? exc : ""))) return -1;
    if (csv_sep(f) || csv_field(f, enc)) return -1;
    if (csv_sep(f) || csv_field(f, exc)) return -1;
    if (csv_sep(f) || csv_field(f, label ? label : "")) return -1;
    return csv_eol(f);
}

int sqs_csv_write(FILE *f, const SqsCard *card, const SqfStation *stations, size_t n_stations,
                  const SqsCsvOpts *opts) {
    SqsCsvOpts def = {0};
    if (!opts) opts = &def;
    if (!f || !card) return -1;

    /* Header */
    if (fputs("index,brand,system_code", f) < 0) return -1;
    if (opts->include_idm && fputs(",idm,pmm", f) < 0) return -1;
    if (fputs(",card_balance_yen,date,time,kind,fare_yen,balance_yen,"
              "enter_station,exit_station,enter_code,exit_code,process_label\n",
              f) < 0)
        return -1;

    if (!card->n_history) return emit_row(f, card, NULL, stations, n_stations, opts, 0);

    for (size_t i = 0; i < card->n_history; i++) {
        if (emit_row(f, card, &card->history[i], stations, n_stations, opts, (int)i + 1) != 0) return -1;
    }
    return 0;
}

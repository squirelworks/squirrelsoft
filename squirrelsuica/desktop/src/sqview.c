/* sqview: card → display rows for CLI and GUI. Original code. */
#include "sqview.h"

#include <stdio.h>
#include <string.h>

static void hex8(const uint8_t *b, char *out) {
    for (size_t i = 0; i < 8; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

static void date_of(const SqfHistory *h, char *out, size_t n) {
    if (h->year) snprintf(out, n, "%04d-%02d-%02d", h->year, h->month, h->day);
    else snprintf(out, n, "?\?\?\?-?\?-?\?");
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

void sqs_view_build(SqsView *out, const SqsCard *card, const SqfStation *stations, size_t n_stations) {
    memset(out, 0, sizeof *out);
    snprintf(out->brand, sizeof out->brand, "%s", sqs_brand_label(card->brand));
    snprintf(out->brand_id, sizeof out->brand_id, "%s", sqs_brand_id(card->brand));
    hex8(card->idm, out->idm);
    hex8(card->pmm, out->pmm);
    snprintf(out->system, sizeof out->system, "%04x", card->system_code);
    out->balance_yen = card->balance_yen;
    if (card->balance_yen >= 0)
        snprintf(out->balance, sizeof out->balance, "Balance ¥%d", (int)card->balance_yen);
    else
        snprintf(out->balance, sizeof out->balance, "Balance (unknown)");

    out->n_rows = card->n_history;
    if (out->n_rows > SQS_MAX_HISTORY) out->n_rows = SQS_MAX_HISTORY;
    for (size_t i = 0; i < out->n_rows; i++) {
        const SqfHistory *h = &card->history[i];
        SqsViewRow *r = &out->rows[i];
        date_of(h, r->date, sizeof r->date);
        snprintf(r->kind, sizeof r->kind, "%s", sqf_kind_name(h->kind));
        if (h->fare_yen) snprintf(r->fare, sizeof r->fare, "fare ¥%d", (int)h->fare_yen);
        else snprintf(r->fare, sizeof r->fare, "fare —");
        snprintf(r->balance, sizeof r->balance, "bal ¥%d", (int)h->balance_yen);

        const char *en = station_name(stations, n_stations, h, 0);
        const char *ex = station_name(stations, n_stations, h, 1);
        char enc[8], exc[8];
        code_of(h->has_enter, h->enter_line, h->enter_station, enc, sizeof enc);
        code_of(h->has_exit, h->exit_line, h->exit_station, exc, sizeof exc);
        const char *a = en ? en : (enc[0] ? enc : NULL);
        const char *b = ex ? ex : (exc[0] ? exc : NULL);
        if (a && b && strcmp(a, b))
            snprintf(r->route, sizeof r->route, "%s → %s", a, b);
        else if (a || b)
            snprintf(r->route, sizeof r->route, "%s", a ? a : b);
        else
            r->route[0] = 0;

        const char *label = card->brand == SQS_SUICA ? sqf_suica_process_label(h->process) : NULL;
        r->detail[0] = 0;
        if (label || h->hour >= 0) {
            if (label && h->hour >= 0)
                snprintf(r->detail, sizeof r->detail, "%s · %02d:%02d", label, h->hour, h->minute);
            else if (label)
                snprintf(r->detail, sizeof r->detail, "%s", label);
            else
                snprintf(r->detail, sizeof r->detail, "%02d:%02d", h->hour, h->minute);
        }
    }
}

void sqs_view_print_text(FILE *f, const SqsView *v) {
    fprintf(f, "Brand %s\nIDm %s\nPMm %s\nSystem %s\n%s\nHistory:\n", v->brand, v->idm, v->pmm, v->system,
            v->balance);
    if (!v->n_rows) fputs("  (none)\n", f);
    for (size_t i = 0; i < v->n_rows; i++) {
        const SqsViewRow *r = &v->rows[i];
        fprintf(f, "  %zu. %s  %-6s  %s  %s", i + 1, r->date, r->kind, r->fare, r->balance);
        if (r->route[0]) fprintf(f, "  %s", r->route);
        if (r->detail[0]) fprintf(f, "\n      %s", r->detail);
        fputc('\n', f);
    }
}

/* sqrinex: RINEX GPS navigation and observation files (see sqrinex.h).
 * Original code, written from the public format descriptions. */
#include "sqrinex.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- time ---- */

/* days since 1970-01-01 of a proleptic Gregorian date */
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, int *y, int *m, int *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

void sqr_gps_time(int year, int month, int day, int hour, int min, double sec, int *week, double *sow) {
    long days = days_from_civil(year, month, day) - days_from_civil(1980, 1, 6);
    long w = days >= 0 ? days / 7 : (days - 6) / 7;
    *week = (int)w;
    *sow = (double)(days - w * 7) * 86400.0 + hour * 3600.0 + min * 60.0 + sec;
}

void sqr_calendar(int week, double sow, int *year, int *month, int *day, int *hour, int *min, double *sec) {
    double whole = floor(sow / 86400.0);
    long days = (long)week * 7 + (long)whole + days_from_civil(1980, 1, 6);
    civil_from_days(days, year, month, day);
    double s = sow - whole * 86400.0;
    *hour = (int)(s / 3600.0);
    s -= *hour * 3600.0;
    *min = (int)(s / 60.0);
    *sec = s - *min * 60.0;
}

/* ---- lines ---- */

static int fail(SqrError *err, int line, const char *msg) {
    if (err) {
        err->code = -1;
        err->line = line;
        snprintf(err->msg, sizeof err->msg, "%s", msg);
    }
    return -1;
}

/* the line at *p (without its end) into buf; advances; 0 at the end of the text */
static int next_line(const char **p, char *buf, size_t cap) {
    if (!**p) return 0;
    size_t n = strcspn(*p, "\r\n");
    size_t k = n < cap - 1 ? n : cap - 1;
    memcpy(buf, *p, k);
    buf[k] = 0;
    *p += n;
    if (**p == '\r') (*p)++;
    if (**p == '\n') (*p)++;
    return 1;
}

/* the header label in columns 61-80 */
static int is_label(const char *line, const char *label) {
    return strlen(line) > 60 && strncmp(line + 60, label, strlen(label)) == 0;
}

/* a fixed-width number field (Fortran D exponents allowed); blank is 0 */
static double field(const char *line, int start, int width, int *ok) {
    char t[40];
    int len = (int)strlen(line);
    if (start >= len) return 0;
    int n = width < (int)sizeof t - 1 ? width : (int)sizeof t - 1;
    if (start + n > len) n = len - start;
    memcpy(t, line + start, (size_t)n);
    t[n] = 0;
    int blank = 1;
    for (char *c = t; *c; c++) {
        if (*c == 'D' || *c == 'd') *c = 'E';
        if (*c != ' ') blank = 0;
    }
    if (blank) return 0;
    char *end;
    double v = strtod(t, &end);
    while (*end == ' ') end++;
    if (*end && ok) *ok = 0;
    return v;
}

static int ifield(const char *line, int start, int width, int *ok) { return (int)lround(field(line, start, width, ok)); }

/* ---- navigation ---- */

int sqr_read_nav(const char *text, SqrNav *nav, SqrError *err) {
    char line[256];
    const char *p = text;
    int ln = 0, version3 = 0, header = 1;
    nav->n = nav->skipped = 0;
    double version = 0;
    while (header) {
        if (!next_line(&p, line, sizeof line)) return fail(err, ln, "no END OF HEADER");
        ln++;
        if (is_label(line, "RINEX VERSION / TYPE")) {
            version = field(line, 0, 9, NULL);
            if (line[20] != 'N' && line[20] != 'n') return fail(err, ln, "not a navigation file");
            if (version >= 3 && line[40] != 'G' && line[40] != 'M')
                return fail(err, ln, "not a GPS or mixed navigation file");
        } else if (is_label(line, "END OF HEADER")) {
            header = 0;
        }
    }
    if (version < 2 || version >= 5) return fail(err, 1, "unknown RINEX version");
    version3 = version >= 3;
    int first = version3 ? 23 : 22, step = version3 ? 4 : 3; /* where the numbers start */
    for (;;) {
        if (!next_line(&p, line, sizeof line)) break;
        ln++;
        if (strspn(line, " ") == strlen(line)) continue;
        int start_ln = ln, ok = 1, prn;
        char sys = 'G';
        int y, mo, d, h, mi;
        double s;
        if (version3) {
            sys = line[0];
            prn = ifield(line, 1, 2, &ok);
            y = ifield(line, 4, 4, &ok);
            mo = ifield(line, 9, 2, &ok);
            d = ifield(line, 12, 2, &ok);
            h = ifield(line, 15, 2, &ok);
            mi = ifield(line, 18, 2, &ok);
            s = field(line, 21, 2, &ok);
        } else {
            prn = ifield(line, 0, 2, &ok);
            y = ifield(line, 3, 2, &ok);
            y += y < 80 ? 2000 : 1900;
            mo = ifield(line, 6, 2, &ok);
            d = ifield(line, 9, 2, &ok);
            h = ifield(line, 12, 2, &ok);
            mi = ifield(line, 15, 2, &ok);
            s = field(line, 17, 5, &ok);
        }
        /* how many more lines this system's record has */
        int more = sys == 'R' || sys == 'S' ? 3 : 7;
        if (sys != 'G') { /* other systems: count the record's lines, do not read them */
            for (int k = 0; k < more; k++) {
                if (!next_line(&p, line, sizeof line)) return fail(err, ln + 1, "navigation record cut short");
                ln++;
            }
            nav->skipped++;
            continue;
        }
        double v[32] = {0};
        v[0] = field(line, first, 19, &ok);
        v[1] = field(line, first + 19, 19, &ok);
        v[2] = field(line, first + 38, 19, &ok);
        for (int k = 0; k < more; k++) {
            if (!next_line(&p, line, sizeof line)) {
                if (k >= 6) break; /* the last line may be cut short */
                return fail(err, ln + 1, "navigation record cut short");
            }
            ln++;
            for (int j = 0; j < 4; j++) v[3 + 4 * k + j] = field(line, step + 19 * j, 19, &ok);
        }
        if (!ok) return fail(err, start_ln, "a number in a navigation record does not parse");
        if (prn < 1 || prn > 99 || mo < 1 || mo > 12 || d < 1 || d > 31)
            return fail(err, start_ln, "bad satellite or date in a navigation record");
        if (nav->n >= SQR_MAX_NAV) return fail(err, start_ln, "too many navigation records");
        SqgEphemeris *e = &nav->eph[nav->n++];
        memset(e, 0, sizeof *e);
        int week;
        sqr_gps_time(y, mo, d, h, mi, s, &week, &e->toc);
        e->prn = prn;
        e->af0 = v[0];
        e->af1 = v[1];
        e->af2 = v[2];
        /* v[3] IODE */ e->crs = v[4];
        e->delta_n = v[5];
        e->m0 = v[6];
        e->cuc = v[7];
        e->e = v[8];
        e->cus = v[9];
        e->sqrt_a = v[10];
        e->toe = v[11];
        e->cic = v[12];
        e->omega0 = v[13];
        e->cis = v[14];
        e->i0 = v[15];
        e->crc = v[16];
        e->w = v[17];
        e->omega_dot = v[18];
        e->idot = v[19];
        /* v[20] L2 codes */ e->week = (int)lround(v[21]);
        /* v[22] L2 P flag, v[23] accuracy */ e->health = (int)lround(v[24]);
        e->tgd = v[25];
        if (e->week < 1024 && week >= 1024) e->week += (week - e->week + 512) / 1024 * 1024; /* 10-bit weeks */
        if (!(e->sqrt_a > 1000) || !(e->e >= 0 && e->e < 1))
            return fail(err, start_ln, "orbit numbers out of range in a navigation record");
    }
    return nav->n;
}

int sqr_nav_select(const SqrNav *nav, int week, double sow, SqgEphemeris *out, int cap) {
    int n = 0;
    for (int prn = 1; prn <= 99 && n < cap; prn++) {
        int best = -1;
        double best_dt = 4 * 3600.0 + 1;
        for (int i = 0; i < nav->n; i++) {
            const SqgEphemeris *e = &nav->eph[i];
            if (e->prn != prn || e->health) continue;
            double dt = fabs((week - e->week) * SQG_WEEK + sow - e->toe);
            if (dt < best_dt) {
                best_dt = dt;
                best = i;
            }
        }
        if (best >= 0) out[n++] = nav->eph[best];
    }
    return n;
}

/* ---- observations ---- */

/* observation types from a header line: RINEX 3 has " C1C" in 4 columns
 * from column 7, RINEX 2 has "    C1" in 6 columns from column 7 */
static void add_types(const char *line, int v3, char (*types)[4], int *have) {
    int width = v3 ? 4 : 6, len = (int)strlen(line);
    for (int at = 6; at + width <= 60 && at + width <= len && *have < 64; at += width) {
        const char *t = line + at + (v3 ? 1 : 4);
        int n = v3 ? 3 : 2;
        if (t[0] == ' ') break;
        memcpy(types[*have], t, (size_t)n);
        types[(*have)++][n] = 0;
    }
}

int sqr_obs_open(SqrObs *r, const char *text, SqrError *err) {
    memset(r, 0, sizeof *r);
    r->p = text;
    r->snr_index = -1;
    char line[256], types[64][4];
    int have = 0, want = 0, gps = 0, version3 = 0;
    for (;;) {
        if (!next_line(&r->p, line, sizeof line)) return fail(err, r->line, "no END OF HEADER");
        r->line++;
        if (is_label(line, "RINEX VERSION / TYPE")) {
            r->version = field(line, 0, 9, NULL);
            version3 = r->version >= 3;
            if (line[20] != 'O' && line[20] != 'o') return fail(err, r->line, "not an observation file");
            char sys = line[40];
            if (sys != 'G' && sys != 'M' && sys != ' ') return fail(err, r->line, "no GPS observations in this file");
        } else if (is_label(line, "APPROX POSITION XYZ")) {
            for (int k = 0; k < 3; k++) r->approx[k] = field(line, 14 * k, 14, NULL);
        } else if (version3 && is_label(line, "SYS / # / OBS TYPES")) {
            if (line[0] == 'G') {
                gps = 1;
                want = ifield(line, 3, 3, NULL);
                add_types(line, 1, types, &have);
            } else if (line[0] == ' ' && gps && have < want) {
                add_types(line, 1, types, &have);
            } else {
                gps = 0;
            }
        } else if (!version3 && is_label(line, "# / TYPES OF OBSERV")) {
            if (line[5] != ' ' || want == 0) want = ifield(line, 0, 6, NULL);
            add_types(line, 0, types, &have);
        } else if (is_label(line, "END OF HEADER")) {
            break;
        }
    }
    if (r->version < 2 || r->version >= 5) return fail(err, 1, "unknown RINEX version");
    r->n_types = have;
    static const char *CODES[] = {"C1C", "C1", "C1W", "P1", "C1P"};
    r->code_index = -1;
    for (size_t c = 0; c < sizeof CODES / sizeof *CODES && r->code_index < 0; c++)
        for (int k = 0; k < have; k++)
            if (!strcmp(types[k], CODES[c])) {
                r->code_index = k;
                snprintf(r->code_name, sizeof r->code_name, "%s", CODES[c]);
                break;
            }
    for (int k = 0; k < have; k++)
        if (!strcmp(types[k], "S1C") || !strcmp(types[k], "S1")) r->snr_index = k;
    static const char *PHASES[] = {"L1C", "L1", "L1W", "L1P"};
    r->carrier_index = -1;
    for (size_t c = 0; c < sizeof PHASES / sizeof *PHASES && r->carrier_index < 0; c++)
        for (int k = 0; k < have; k++)
            if (!strcmp(types[k], PHASES[c])) {
                r->carrier_index = k;
                break;
            }
    if (r->code_index < 0) return fail(err, r->line, "no GPS L1 pseudorange (C1C, C1, C1W or P1)");
    return 0;
}

/* observation k of a satellite, from its lines joined (16 columns each) */
static double obs_value(const char *sat, int k, int *present) {
    int start = k * 16;
    if (start >= (int)strlen(sat)) {
        *present = 0;
        return 0;
    }
    char t[16];
    memcpy(t, sat + start, 14);
    t[14] = 0;
    *present = strspn(t, " ") != strlen(t);
    return *present ? strtod(t, NULL) : 0;
}

int sqr_obs_next(SqrObs *r, SqgEpoch *ep, int *week, SqrError *err) {
    char line[256];
    int version3 = r->version >= 3;
    for (;;) {
        if (!next_line(&r->p, line, sizeof line)) return 0;
        r->line++;
        if (strspn(line, " ") == strlen(line)) continue;
        int ok = 1, y, mo, d, h, mi, flag, n;
        double s;
        if (version3) {
            if (line[0] != '>') return fail(err, r->line, "expected an epoch line starting with >");
            y = ifield(line, 2, 4, &ok);
            mo = ifield(line, 7, 2, &ok);
            d = ifield(line, 10, 2, &ok);
            h = ifield(line, 13, 2, &ok);
            mi = ifield(line, 16, 2, &ok);
            s = field(line, 18, 11, &ok);
            flag = ifield(line, 31, 1, &ok);
            n = ifield(line, 32, 3, &ok);
        } else {
            y = ifield(line, 1, 2, &ok);
            y += y < 80 ? 2000 : 1900;
            mo = ifield(line, 4, 2, &ok);
            d = ifield(line, 7, 2, &ok);
            h = ifield(line, 10, 2, &ok);
            mi = ifield(line, 13, 2, &ok);
            s = field(line, 15, 11, &ok);
            flag = ifield(line, 28, 1, &ok);
            n = ifield(line, 29, 3, &ok);
        }
        if (!ok || n < 0 || n > 999) return fail(err, r->line, "bad epoch line");
        if (flag > 1) { /* an event: n header lines follow */
            for (int k = 0; k < n; k++) {
                if (!next_line(&r->p, line, sizeof line)) return 0;
                r->line++;
            }
            continue;
        }
        if (mo < 1 || mo > 12 || d < 1 || d > 31) return fail(err, r->line, "bad date in an epoch line");
        memset(ep, 0, sizeof *ep);
        sqr_gps_time(y, mo, d, h, mi, s, week, &ep->t_receiver);
        /* RINEX 2 lists the satellites on the epoch line(s), 12 per line */
        char list[1000][4];
        int lines_per_sat = version3 ? 1 : (r->n_types + 4) / 5;
        if (!version3) {
            for (int k = 0; k < n; k++) {
                if (k && k % 12 == 0) {
                    if (!next_line(&r->p, line, sizeof line)) return fail(err, r->line, "epoch cut short");
                    r->line++;
                }
                int at = 32 + (k % 12) * 3;
                if (at + 3 > (int)strlen(line)) return fail(err, r->line, "satellite list cut short");
                memcpy(list[k], line + at, 3);
                list[k][3] = 0;
                if (list[k][0] == ' ') list[k][0] = 'G';
            }
        }
        for (int k = 0; k < n; k++) {
            char sat[16 * 64 + 8] = "";
            char id[4];
            for (int j = 0; j < lines_per_sat; j++) {
                if (!next_line(&r->p, line, sizeof line)) return fail(err, r->line, "epoch cut short");
                r->line++;
                const char *body = line;
                if (version3) {
                    memcpy(id, line, 3);
                    id[3] = 0;
                    body = strlen(line) > 3 ? line + 3 : "";
                } else {
                    memcpy(id, list[k], 4);
                }
                /* pad each line to 5 full fields so the next one lines up */
                char padded[96];
                snprintf(padded, sizeof padded, "%-80.80s", body);
                size_t have = strlen(sat);
                if (version3) snprintf(sat + have, sizeof sat - have, "%s", body);
                else snprintf(sat + have, sizeof sat - have, "%s", padded);
            }
            if (id[0] != 'G') continue;
            int present;
            double c1 = obs_value(sat, r->code_index, &present);
            if (!present || !(c1 > 1e6) || ep->n_obs >= SQG_MAX_SATS) continue;
            SqgObservation *o = &ep->obs[ep->n_obs++];
            o->prn = atoi(id + 1);
            o->pseudorange_m = c1;
            o->snr_dbhz = r->snr_index >= 0 ? obs_value(sat, r->snr_index, &present) : 0;
            o->valid = 1;
            if (r->carrier_index >= 0) {
                o->carrier_cyc = obs_value(sat, r->carrier_index, &present);
                o->has_carrier = present && o->carrier_cyc != 0;
            }
        }
        return 1;
    }
}

/* ---- writers (RINEX 3.04) ---- */

static void label(FILE *f, const char *body, const char *lab) { fprintf(f, "%-60.60s%-20s\n", body, lab); }

void sqr_write_nav(FILE *f, const SqgEphemeris *ephs, int n) {
    label(f, "     3.04           N: GNSS NAV DATA    G: GPS", "RINEX VERSION / TYPE");
    label(f, "squirrelgnss", "PGM / RUN BY / DATE");
    label(f, "", "END OF HEADER");
    for (int i = 0; i < n; i++) {
        const SqgEphemeris *e = &ephs[i];
        int y, mo, d, h, mi;
        double s;
        sqr_calendar(e->week, e->toc, &y, &mo, &d, &h, &mi, &s);
        double v[28] = {0, e->crs, e->delta_n, e->m0, e->cuc, e->e, e->cus, e->sqrt_a, e->toe, e->cic, e->omega0,
                        e->cis, e->i0, e->crc, e->w, e->omega_dot, e->idot, 1, e->week, 0, 2.0, e->health, e->tgd, 0,
                        e->toc, 4};
        fprintf(f, "G%02d %04d %02d %02d %02d %02d %02d%19.12E%19.12E%19.12E\n", e->prn, y, mo, d, h, mi,
                (int)lround(s), e->af0, e->af1, e->af2);
        for (int k = 0; k < 7; k++) {
            fprintf(f, "    ");
            int cols = k == 6 ? 2 : 4;
            for (int j = 0; j < cols; j++) fprintf(f, "%19.12E", v[4 * k + j]);
            fprintf(f, "\n");
        }
    }
}

void sqr_write_obs_header(FILE *f, const SqgEcef *approx) {
    char t[80];
    label(f, "     3.04           OBSERVATION DATA    G: GPS", "RINEX VERSION / TYPE");
    label(f, "squirrelgnss", "PGM / RUN BY / DATE");
    label(f, "SYNTHETIC", "MARKER NAME");
    snprintf(t, sizeof t, "%14.4f%14.4f%14.4f", approx ? approx->x : 0, approx ? approx->y : 0, approx ? approx->z : 0);
    label(f, t, "APPROX POSITION XYZ");
    label(f, "G    3 C1C L1C S1C", "SYS / # / OBS TYPES");
    label(f, "", "END OF HEADER");
}

void sqr_write_obs_epoch(FILE *f, int week, const SqgEpoch *ep) {
    int y, mo, d, h, mi, n = 0;
    double s;
    sqr_calendar(week, ep->t_receiver, &y, &mo, &d, &h, &mi, &s);
    for (int i = 0; i < ep->n_obs; i++) n += ep->obs[i].valid;
    fprintf(f, "> %04d %02d %02d %02d %02d%11.7f  0%3d\n", y, mo, d, h, mi, s, n);
    for (int i = 0; i < ep->n_obs; i++)
        if (ep->obs[i].valid)
        {
            const SqgObservation *o = &ep->obs[i];
            fprintf(f, "G%02d%14.3f  ", o->prn, o->pseudorange_m);
            if (o->has_carrier) fprintf(f, "%14.3f  ", o->carrier_cyc);
            else fprintf(f, "%16s", "");
            fprintf(f, "%14.3f  \n", o->snr_dbhz);
        }
}

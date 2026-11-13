/* sqrecv: NMEA, UBX and the GPS navigation message (see sqrecv.h). Original code. */
#include "sqrecv.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPS_PI 3.1415926535898 /* the value IS-GPS-200 converts semicircles with */

/* ---- framing ---- */

enum { F_IDLE, F_NMEA, F_SYNC2, F_HDR, F_PAYLOAD, F_CK };

void sqrx_framer_init(SqrxFramer *f) { memset(f, 0, sizeof *f); }

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = toupper(c);
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int sqrx_nmea_checksum_ok(const char *s) {
    if (*s != '$') return 0;
    unsigned x = 0;
    const char *p = s + 1;
    for (; *p && *p != '*'; p++) x ^= (unsigned char)*p;
    if (*p != '*') return 0;
    int h = hexval(p[1]), l = h >= 0 ? hexval(p[2]) : -1;
    return l >= 0 && (unsigned)(h * 16 + l) == x;
}

int sqrx_framer_byte(SqrxFramer *f, unsigned char b, SqrxFrame *out) {
    f->bytes++;
    switch (f->state) {
    case F_IDLE:
        if (b == '$') {
            f->buf[0] = b;
            f->len = 1;
            f->state = F_NMEA;
        } else if (b == 0xB5)
            f->state = F_SYNC2;
        else if (b != '\r' && b != '\n')
            f->junk++;
        return 0;
    case F_NMEA:
        if (b == '\r' || b == '\n') {
            f->buf[f->len] = 0;
            f->state = F_IDLE;
            if (!sqrx_nmea_checksum_ok((const char *)f->buf)) {
                f->nmea_bad++;
                return 0;
            }
            f->nmea_ok++;
            memset(out, 0, sizeof *out);
            out->kind = SQRX_FRAME_NMEA;
            out->nmea = (const char *)f->buf;
            return 1;
        }
        if (b == '$' || b < 0x20 || b > 0x7E || f->len >= SQRX_MAX_NMEA) {
            /* a broken sentence: start again (a '$' may begin the next one) */
            f->nmea_bad++;
            f->state = F_IDLE;
            if (b == '$' || b == 0xB5) {
                f->bytes--; /* counted again below */
                return sqrx_framer_byte(f, b, out);
            }
            return 0;
        }
        f->buf[f->len++] = b;
        return 0;
    case F_SYNC2:
        if (b == 0x62) {
            f->state = F_HDR;
            f->len = 0;
            f->ck_a = f->ck_b = 0;
        } else {
            f->junk++;
            f->state = F_IDLE;
            if (b == '$' || b == 0xB5) {
                f->bytes--;
                return sqrx_framer_byte(f, b, out);
            }
        }
        return 0;
    case F_HDR:
        f->buf[f->len++] = b;
        f->ck_a += b;
        f->ck_b += f->ck_a;
        if (f->len == 4) {
            f->need = f->buf[2] | f->buf[3] << 8;
            if (f->need > SQRX_MAX_PAYLOAD) {
                f->ubx_bad++;
                f->state = F_IDLE;
                return 0;
            }
            f->state = f->need ? F_PAYLOAD : F_CK;
        }
        return 0;
    case F_PAYLOAD:
        f->buf[f->len++] = b;
        f->ck_a += b;
        f->ck_b += f->ck_a;
        if (f->len == 4 + f->need) f->state = F_CK;
        return 0;
    case F_CK:
        f->buf[f->len++] = b;
        if (f->len < 4 + f->need + 2) return 0;
        f->state = F_IDLE;
        if (f->buf[4 + f->need] != f->ck_a || f->buf[5 + f->need] != f->ck_b) {
            f->ubx_bad++;
            return 0;
        }
        f->ubx_ok++;
        memset(out, 0, sizeof *out);
        out->kind = SQRX_FRAME_UBX;
        out->cls = f->buf[0];
        out->id = f->buf[1];
        out->payload = f->buf + 4;
        out->len = f->need;
        return 1;
    }
    f->state = F_IDLE;
    return 0;
}

/* ---- NMEA ---- */

void sqrx_nmea_init(SqrxNmea *n) { memset(n, 0, sizeof *n); }

/* split "$GPGGA,a,b*HH" into fields (the first is the address); returns the count */
static int split(const char *s, char *buf, size_t cap, char **fld, int max) {
    size_t len = strcspn(s + 1, "*");
    if (len >= cap) return 0;
    memcpy(buf, s + 1, len);
    buf[len] = 0;
    int n = 0;
    char *p = buf;
    while (n < max) {
        fld[n++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = 0;
        p = c + 1;
    }
    return n;
}

static int num(const char *s, double *v) {
    if (!*s) return 0;
    char *end;
    *v = strtod(s, &end);
    return *end == 0 && isfinite(*v);
}

/* ddmm.mmmm with a hemisphere letter */
static int angle(const char *v, const char *hemi, int deg_digits, double *out) {
    double x;
    if (!num(v, &x) || x < 0) return 0;
    double deg = floor(x / 100), min = x - deg * 100;
    if (min >= 60 || (int)strlen(v) < deg_digits) return 0;
    *out = deg + min / 60;
    if (!strcmp(hemi, "S") || !strcmp(hemi, "W")) *out = -*out;
    else if (strcmp(hemi, "N") && strcmp(hemi, "E")) return 0;
    return 1;
}

static int hms(const char *s, double *out) {
    double t;
    if (!num(s, &t) || t < 0) return 0;
    int hh = (int)(t / 10000), mm = (int)fmod(t / 100, 100);
    double ss = fmod(t, 100);
    if (hh > 23 || mm > 59 || ss >= 61) return 0;
    *out = hh * 3600.0 + mm * 60.0 + ss;
    return 1;
}

static char sys_of(const char *talker) {
    if (!strncmp(talker, "GP", 2)) return 'P';
    if (!strncmp(talker, "GL", 2)) return 'L';
    if (!strncmp(talker, "GA", 2)) return 'A';
    if (!strncmp(talker, "GB", 2) || !strncmp(talker, "BD", 2)) return 'B';
    if (!strncmp(talker, "GQ", 2)) return 'Q';
    return 'N';
}

int sqrx_nmea_parse(SqrxNmea *n, const char *s) {
    if (!sqrx_nmea_checksum_ok(s)) return -1;
    char buf[SQRX_MAX_NMEA + 4], *f[40];
    int nf = split(s, buf, sizeof buf, f, 40);
    if (nf < 1 || strlen(f[0]) < 5) return -1;
    const char *type = f[0] + strlen(f[0]) - 3;
    if (!strcmp(type, "GGA")) {
        if (nf < 12) return -1;
        double t, q = 0, sats = 0, hdop = 0, alt = 0, sep = 0;
        if (f[1][0] && !hms(f[1], &t)) return -1;
        if (f[6][0] && !num(f[6], &q)) return -1;
        n->quality = (int)q;
        if (f[1][0]) n->utc_s = t;
        if (f[7][0] && num(f[7], &sats)) n->sats_used = (int)sats;
        if (f[8][0] && num(f[8], &hdop)) n->hdop = hdop;
        if (n->quality == 0 || !f[2][0]) {
            n->have_pos = 0;
            return SQRX_NMEA_GGA;
        }
        double lat, lon;
        if (!angle(f[2], f[3], 4, &lat) || !angle(f[4], f[5], 5, &lon) || !num(f[9], &alt)) return -1;
        if (f[11][0] && !num(f[11], &sep)) return -1;
        n->pos.lat_deg = lat;
        n->pos.lon_deg = lon;
        n->msl_m = alt;
        n->geoid_sep_m = sep;
        n->pos.height_m = alt + sep;
        n->have_pos = 1;
        return SQRX_NMEA_GGA;
    }
    if (!strcmp(type, "RMC")) {
        if (nf < 10) return -1;
        double t, kn = 0, crs = 0, date;
        if (f[1][0] && !hms(f[1], &t)) return -1;
        if (f[1][0]) n->utc_s = t;
        n->rmc_valid = !strcmp(f[2], "A");
        if (f[7][0] && !num(f[7], &kn)) return -1;
        if (f[8][0] && !num(f[8], &crs)) return -1;
        n->speed_mps = kn * 1852.0 / 3600.0;
        n->course_deg = crs;
        if (f[9][0]) {
            if (!num(f[9], &date) || strlen(f[9]) != 6) return -1;
            int d = (int)date;
            n->day = d / 10000;
            n->month = d / 100 % 100;
            n->year = (d % 100 < 80 ? 2000 : 1900) + d % 100; /* two digits: 1980 to 2079 */
            if (n->day < 1 || n->day > 31 || n->month < 1 || n->month > 12) return -1;
            n->have_date = 1;
        }
        return SQRX_NMEA_RMC;
    }
    if (!strcmp(type, "GSA")) {
        if (nf < 18) return -1;
        double mode = 0, v;
        if (f[2][0] && !num(f[2], &mode)) return -1;
        n->fix_mode = (int)mode;
        n->n_used_prn = 0;
        for (int i = 3; i < 15; i++)
            if (f[i][0] && num(f[i], &v)) n->used_prn[n->n_used_prn++] = (int)v;
        if (f[15][0] && num(f[15], &v)) n->pdop = v;
        if (f[16][0] && num(f[16], &v)) n->hdop = v;
        if (f[17][0] && num(f[17], &v)) n->vdop = v;
        return SQRX_NMEA_GSA;
    }
    if (!strcmp(type, "GSV")) {
        double msgs, msg, total;
        if (nf < 4 || !num(f[1], &msgs) || !num(f[2], &msg) || !num(f[3], &total)) return -1;
        if (msgs < 1 || msg < 1 || msg > msgs || msgs > 16) return -1;
        char sys = sys_of(f[0]);
        if ((int)msg == 1) {
            n->gsv_n = 0;
            n->gsv_msgs = (int)msgs;
            n->gsv_next = 1;
            n->gsv_sys = sys;
        }
        if ((int)msg != n->gsv_next || sys != n->gsv_sys || (int)msgs != n->gsv_msgs) {
            n->gsv_next = 0; /* out of order: wait for the next set */
            return SQRX_NMEA_GSV;
        }
        for (int i = 4; i + 3 < nf + 1 && i < nf; i += 4) {
            double prn, el = 0, az = 0, snr = 0;
            if (!num(f[i], &prn)) continue;
            if (i + 1 < nf && f[i + 1][0]) num(f[i + 1], &el);
            if (i + 2 < nf && f[i + 2][0]) num(f[i + 2], &az);
            if (i + 3 < nf && f[i + 3][0]) num(f[i + 3], &snr);
            if (n->gsv_n < SQRX_MAX_VIEW) n->gsv[n->gsv_n++] = (SqrxSatView){sys, (int)prn, el, az, snr};
        }
        n->gsv_next++;
        if ((int)msg == n->gsv_msgs) {
            /* the set is whole: it replaces that system's satellites */
            int k = 0;
            for (int i = 0; i < n->n_view; i++)
                if (n->view[i].sys != sys) n->view[k++] = n->view[i];
            for (int i = 0; i < n->gsv_n && k < SQRX_MAX_VIEW; i++) n->view[k++] = n->gsv[i];
            n->n_view = k;
            n->gsv_next = 0;
        }
        return SQRX_NMEA_GSV;
    }
    return SQRX_NMEA_OTHER;
}

/* "$body*HH\r\n" */
static size_t finish(char *out, size_t cap, const char *body) {
    unsigned x = 0;
    for (const char *p = body; *p; p++) x ^= (unsigned char)*p;
    int n = snprintf(out, cap, "$%s*%02X\r\n", body, x);
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

static void ddmm(char *buf, size_t cap, double deg, int width, char pos, char neg, char *hemi) {
    double a = fabs(deg);
    int d = (int)a;
    double m = (a - d) * 60;
    if (m >= 59.999999995) d++, m = 0;
    snprintf(buf, cap, "%0*d%011.8f", width, d, m);
    *hemi = deg < 0 ? neg : pos;
}

static void utc(char *buf, size_t cap, double s) {
    s = fmod(s, 86400.0);
    int hh = (int)(s / 3600), mm = (int)fmod(s / 60, 60);
    snprintf(buf, cap, "%02d%02d%05.2f", hh, mm, fmod(s, 60));
}

size_t sqrx_nmea_gga(char *out, size_t cap, double utc_s, SqgGeodetic pos, int quality, int sats, double hdop) {
    char t[16], la[24], lo[24], body[160], hn, he;
    utc(t, sizeof t, utc_s);
    ddmm(la, sizeof la, pos.lat_deg, 2, 'N', 'S', &hn);
    ddmm(lo, sizeof lo, pos.lon_deg, 3, 'E', 'W', &he);
    /* the height is given above the ellipsoid with no geoid separation */
    snprintf(body, sizeof body, "GPGGA,%s,%s,%c,%s,%c,%d,%02d,%.1f,%.3f,M,0.0,M,,", t, la, hn, lo, he, quality, sats,
             hdop, pos.height_m);
    return finish(out, cap, body);
}

size_t sqrx_nmea_rmc(char *out, size_t cap, double utc_s, int year, int month, int day, SqgGeodetic pos,
                     double speed_mps, double course_deg) {
    char t[16], la[24], lo[24], body[160], hn, he;
    utc(t, sizeof t, utc_s);
    ddmm(la, sizeof la, pos.lat_deg, 2, 'N', 'S', &hn);
    ddmm(lo, sizeof lo, pos.lon_deg, 3, 'E', 'W', &he);
    snprintf(body, sizeof body, "GPRMC,%s,A,%s,%c,%s,%c,%.3f,%.1f,%02d%02d%02d,,,A", t, la, hn, lo, he,
             speed_mps * 3600.0 / 1852.0, course_deg, day, month, year % 100);
    return finish(out, cap, body);
}

size_t sqrx_nmea_gsv(char *out, size_t cap, const SqrxSatView *sats, int n) {
    int msgs = n ? (n + 3) / 4 : 1;
    size_t used = 0;
    for (int m = 0; m < msgs; m++) {
        char body[200];
        int k = snprintf(body, sizeof body, "GPGSV,%d,%d,%02d", msgs, m + 1, n);
        for (int i = 4 * m; i < n && i < 4 * m + 4; i++)
            k += snprintf(body + k, sizeof body - (size_t)k, ",%02d,%02d,%03d,%02d", sats[i].prn,
                          (int)lround(sats[i].el_deg), (int)lround(fmod(sats[i].az_deg + 360.0, 360.0)) % 360,
                          (int)lround(sats[i].snr_dbhz));
        size_t got = finish(out + used, cap - used, body);
        if (!got) return 0;
        used += got;
    }
    return used;
}

/* ---- LNAV bits ---- */

/* data bit `pos` of a subframe's 240 (24 per word, most significant first) */
static unsigned long getu(const SqrxSubframe w, int pos, int len) {
    unsigned long v = 0;
    for (int b = pos; b < pos + len; b++) v = v << 1 | ((w[b / 24] >> 6 >> (23 - b % 24)) & 1u);
    return v;
}

static long long gets_(const SqrxSubframe w, int pos, int len) {
    unsigned long v = getu(w, pos, len);
    long long x = (long long)v;
    if (v >> (len - 1) & 1) x -= 1LL << len;
    return x;
}

static void setu(SqrxSubframe w, int pos, int len, unsigned long v) {
    for (int b = pos; b < pos + len; b++) {
        uint32_t bit = (uint32_t)((v >> (pos + len - 1 - b)) & 1u) << (23 - b % 24) << 6;
        w[b / 24] = (w[b / 24] & ~((uint32_t)1 << (23 - b % 24) << 6)) | bit;
    }
}

/* value / scale rounded into a field; 0, or -1 when it does not fit */
static int put(SqrxSubframe w, int pos, int len, double value, double scale, int is_signed) {
    double q = round(value / scale);
    double lo = is_signed ? -ldexp(1, len - 1) : 0, hi = is_signed ? ldexp(1, len - 1) - 1 : ldexp(1, len) - 1;
    if (!(q >= lo && q <= hi)) return -1;
    long long iq = (long long)q;
    setu(w, pos, len, (unsigned long)((unsigned long long)iq & ((len == 32 ? 0xFFFFFFFFULL : (1ULL << len) - 1))));
    return 0;
}

static double wrap_sc(double rad) {
    double sc = rad / GPS_PI;
    sc = fmod(sc + 1.0, 2.0);
    if (sc < 0) sc += 2.0;
    return sc - 1.0;
}

int sqrx_lnav_encode(const SqgEphemeris *e, int iode, double tow_s, SqrxSubframe sf[3]) {
    int bad = 0;
    iode &= 0xFF;
    for (int k = 0; k < 3; k++) {
        memset(sf[k], 0, sizeof(SqrxSubframe));
        setu(sf[k], 0, 8, 0x8B);                                        /* preamble */
        setu(sf[k], 24, 17, (unsigned long)((tow_s + 6.0 * (k + 1)) / 6.0) & 0x1FFFF); /* TOW of the next one */
        setu(sf[k], 43, 3, (unsigned long)(k + 1));                     /* subframe id */
    }
    SqrxSubframe *s1 = &sf[0], *s2 = &sf[1], *s3 = &sf[2];
    setu(*s1, 48, 10, (unsigned long)(e->week & 1023));
    setu(*s1, 64, 6, (unsigned long)(e->health & 63));
    setu(*s1, 70, 2, 0); /* IODC high bits */
    bad |= put(*s1, 160, 8, e->tgd, ldexp(1, -31), 1);
    setu(*s1, 168, 8, (unsigned long)iode);
    bad |= put(*s1, 176, 16, e->toc, 16, 0);
    bad |= put(*s1, 192, 8, e->af2, ldexp(1, -55), 1);
    bad |= put(*s1, 200, 16, e->af1, ldexp(1, -43), 1);
    bad |= put(*s1, 216, 22, e->af0, ldexp(1, -31), 1);

    setu(*s2, 48, 8, (unsigned long)iode);
    bad |= put(*s2, 56, 16, e->crs, ldexp(1, -5), 1);
    bad |= put(*s2, 72, 16, e->delta_n / GPS_PI, ldexp(1, -43), 1);
    bad |= put(*s2, 88, 32, wrap_sc(e->m0), ldexp(1, -31), 1);
    bad |= put(*s2, 120, 16, e->cuc, ldexp(1, -29), 1);
    bad |= put(*s2, 136, 32, e->e, ldexp(1, -33), 0);
    bad |= put(*s2, 168, 16, e->cus, ldexp(1, -29), 1);
    bad |= put(*s2, 184, 32, e->sqrt_a, ldexp(1, -19), 0);
    bad |= put(*s2, 216, 16, e->toe, 16, 0);

    bad |= put(*s3, 48, 16, e->cic, ldexp(1, -29), 1);
    bad |= put(*s3, 64, 32, wrap_sc(e->omega0), ldexp(1, -31), 1);
    bad |= put(*s3, 96, 16, e->cis, ldexp(1, -29), 1);
    bad |= put(*s3, 112, 32, wrap_sc(e->i0), ldexp(1, -31), 1);
    bad |= put(*s3, 144, 16, e->crc, ldexp(1, -5), 1);
    bad |= put(*s3, 160, 32, wrap_sc(e->w), ldexp(1, -31), 1);
    bad |= put(*s3, 192, 24, e->omega_dot / GPS_PI, ldexp(1, -43), 1);
    setu(*s3, 216, 8, (unsigned long)iode);
    bad |= put(*s3, 224, 14, e->idot / GPS_PI, ldexp(1, -43), 1);
    if (fmod(e->toe, 16) != 0 || fmod(e->toc, 16) != 0) bad = -1;
    return bad ? -1 : 0;
}

int sqrx_lnav_decode(const SqrxSubframe sf[3], int week_hint, SqgEphemeris *out) {
    for (int k = 0; k < 3; k++)
        if (getu(sf[k], 43, 3) != (unsigned long)(k + 1)) return -1;
    const SqrxSubframe *s1 = &sf[0], *s2 = &sf[1], *s3 = &sf[2];
    unsigned long iodc = getu(*s1, 70, 2) << 8 | getu(*s1, 168, 8);
    unsigned long iode2 = getu(*s2, 48, 8), iode3 = getu(*s3, 216, 8);
    if (iode2 != iode3 || (iodc & 0xFF) != iode2) return -1;
    SqgEphemeris e;
    memset(&e, 0, sizeof e);
    int wn = (int)getu(*s1, 48, 10);
    e.week = week_hint > 0 ? week_hint - ((week_hint - wn) % 1024 + 1024) % 1024 : 2048 + wn;
    e.health = (int)getu(*s1, 64, 6);
    e.tgd = ldexp((double)gets_(*s1, 160, 8), -31);
    e.toc = getu(*s1, 176, 16) * 16.0;
    e.af2 = ldexp((double)gets_(*s1, 192, 8), -55);
    e.af1 = ldexp((double)gets_(*s1, 200, 16), -43);
    e.af0 = ldexp((double)gets_(*s1, 216, 22), -31);

    e.crs = ldexp((double)gets_(*s2, 56, 16), -5);
    e.delta_n = ldexp((double)gets_(*s2, 72, 16), -43) * GPS_PI;
    e.m0 = ldexp((double)gets_(*s2, 88, 32), -31) * GPS_PI;
    e.cuc = ldexp((double)gets_(*s2, 120, 16), -29);
    e.e = ldexp((double)getu(*s2, 136, 32), -33);
    e.cus = ldexp((double)gets_(*s2, 168, 16), -29);
    e.sqrt_a = ldexp((double)getu(*s2, 184, 32), -19);
    e.toe = getu(*s2, 216, 16) * 16.0;

    e.cic = ldexp((double)gets_(*s3, 48, 16), -29);
    e.omega0 = ldexp((double)gets_(*s3, 64, 32), -31) * GPS_PI;
    e.cis = ldexp((double)gets_(*s3, 96, 16), -29);
    e.i0 = ldexp((double)gets_(*s3, 112, 32), -31) * GPS_PI;
    e.crc = ldexp((double)gets_(*s3, 144, 16), -5);
    e.w = ldexp((double)gets_(*s3, 160, 32), -31) * GPS_PI;
    e.omega_dot = ldexp((double)gets_(*s3, 192, 24), -43) * GPS_PI;
    e.idot = ldexp((double)gets_(*s3, 224, 14), -43) * GPS_PI;
    if (!(e.sqrt_a > 1000)) return -1; /* not an orbit */
    *out = e;
    return 0;
}

/* ---- UBX ---- */

static void put_u2(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
static void put_u4(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> 8 * i);
}
static void put_r8(unsigned char *p, double d) {
    uint64_t v;
    memcpy(&v, &d, 8);
    for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> 8 * i);
}
static void put_r4(unsigned char *p, float d) {
    uint32_t v;
    memcpy(&v, &d, 4);
    put_u4(p, v);
}
static unsigned u2(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t u4(const unsigned char *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t i4(const unsigned char *p) {
    uint32_t v = u4(p);
    return v & 0x80000000u ? -(int32_t)(~v) - 1 : (int32_t)v;
}
static double r8(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    double d;
    memcpy(&d, &v, 8);
    return d;
}

size_t sqrx_ubx_frame(unsigned char *out, size_t cap, int cls, int id, const unsigned char *payload, int len) {
    if (len < 0 || (size_t)len + 8 > cap) return 0;
    out[0] = 0xB5;
    out[1] = 0x62;
    out[2] = (unsigned char)cls;
    out[3] = (unsigned char)id;
    put_u2(out + 4, (unsigned)len);
    if (len) memcpy(out + 6, payload, (size_t)len);
    unsigned char a = 0, b = 0;
    for (int i = 2; i < 6 + len; i++) {
        a += out[i];
        b += a;
    }
    out[6 + len] = a;
    out[7 + len] = b;
    return (size_t)len + 8;
}

size_t sqrx_ubx_cfg_msg(unsigned char *out, size_t cap, int cls, int id, int rate) {
    unsigned char p[3] = {(unsigned char)cls, (unsigned char)id, (unsigned char)rate};
    return sqrx_ubx_frame(out, cap, SQRX_UBX_CFG, SQRX_UBX_CFG_MSG, p, 3);
}

/* GPS signal ids in RXM-RAWX */
enum { SIG_L1CA = 0, SIG_L2CL = 3, SIG_L2CM = 4, SIG_L5I = 6, SIG_L5Q = 7 };

static void meas(unsigned char *m, double pr, double cp, int has_cp, int prn, int sig, int slip, double cno) {
    memset(m, 0, 32);
    put_r8(m, pr);
    put_r8(m + 8, has_cp ? cp : 0);
    put_r4(m + 16, 0.0f);
    m[20] = 0; /* GPS */
    m[21] = (unsigned char)prn;
    m[22] = (unsigned char)sig;
    put_u2(m + 24, slip ? 0 : 64500); /* lock time, ms */
    m[26] = (unsigned char)(cno < 0 ? 0 : cno > 255 ? 255 : lround(cno));
    m[30] = (unsigned char)(1 | (has_cp ? 2 | 4 : 0)); /* pr valid, cp valid, half cycle resolved */
}

size_t sqrx_ubx_rawx(unsigned char *out, size_t cap, const SqgEpoch *ep, int week) {
    static unsigned char p[16 + 32 * 255];
    int n = 0;
    for (int i = 0; i < ep->n_obs && n < 252; i++) {
        const SqgObservation *o = &ep->obs[i];
        if (!o->valid) continue;
        meas(p + 16 + 32 * n++, o->pseudorange_m, o->carrier_cyc, o->has_carrier, o->prn, SIG_L1CA, o->slip,
             o->snr_dbhz);
        if (o->has_l2)
            meas(p + 16 + 32 * n++, o->pseudorange2_m, o->carrier2_cyc, 1, o->prn, SIG_L2CL, o->slip, o->snr_dbhz - 3);
        if (o->has_l5)
            meas(p + 16 + 32 * n++, o->pseudorange5_m, o->carrier5_cyc, o->has_l5_carrier, o->prn, SIG_L5Q, o->slip,
                 o->snr_dbhz + 2);
    }
    memset(p, 0, 16);
    put_r8(p, ep->t_receiver);
    put_u2(p + 8, (unsigned)week);
    p[10] = 18; /* leap seconds */
    p[11] = (unsigned char)n;
    p[12] = 1; /* leap seconds known */
    p[13] = 1; /* version */
    return sqrx_ubx_frame(out, cap, SQRX_UBX_RXM, SQRX_UBX_RXM_RAWX, p, 16 + 32 * n);
}

size_t sqrx_ubx_sfrbx(unsigned char *out, size_t cap, int prn, const SqrxSubframe words) {
    unsigned char p[8 + 40] = {0, (unsigned char)prn, 0, 0, 10, 0, 2, 0};
    for (int i = 0; i < 10; i++) put_u4(p + 8 + 4 * i, words[i]);
    return sqrx_ubx_frame(out, cap, SQRX_UBX_RXM, SQRX_UBX_RXM_SFRBX, p, sizeof p);
}

size_t sqrx_ubx_navpvt(unsigned char *out, size_t cap, const SqrxNavPvt *v) {
    unsigned char p[92];
    memset(p, 0, sizeof p);
    put_u4(p, (uint32_t)llround(v->tow_s * 1000.0));
    put_u2(p + 4, (unsigned)v->year);
    p[6] = (unsigned char)v->month;
    p[7] = (unsigned char)v->day;
    p[8] = (unsigned char)v->hour;
    p[9] = (unsigned char)v->min;
    p[10] = (unsigned char)floor(v->sec);
    p[11] = 0x07; /* date, time, fully resolved */
    put_u4(p + 16, (uint32_t)(int32_t)lround((v->sec - floor(v->sec)) * 1e9));
    p[20] = (unsigned char)v->fix_type;
    p[21] = v->fix_type >= 2 ? 1 : 0;
    p[23] = (unsigned char)v->num_sv;
    put_u4(p + 24, (uint32_t)(int32_t)lround(v->pos.lon_deg * 1e7));
    put_u4(p + 28, (uint32_t)(int32_t)lround(v->pos.lat_deg * 1e7));
    put_u4(p + 32, (uint32_t)(int32_t)lround(v->pos.height_m * 1000));
    put_u4(p + 36, (uint32_t)(int32_t)lround(v->pos.height_m * 1000)); /* no geoid here */
    put_u4(p + 40, (uint32_t)lround(v->h_acc_m * 1000));
    put_u4(p + 44, (uint32_t)lround(v->v_acc_m * 1000));
    put_u4(p + 60, (uint32_t)(int32_t)lround(v->speed_mps * 1000));
    put_u4(p + 64, (uint32_t)(int32_t)lround(v->heading_deg * 1e5));
    put_u2(p + 76, (unsigned)lround(v->pdop * 100));
    return sqrx_ubx_frame(out, cap, SQRX_UBX_NAV, SQRX_UBX_NAV_PVT, p, sizeof p);
}

int sqrx_ubx_navpvt_read(const unsigned char *p, int len, SqrxNavPvt *v) {
    if (len < 84) return -1;
    memset(v, 0, sizeof *v);
    v->tow_s = u4(p) / 1000.0;
    v->year = (int)u2(p + 4);
    v->month = p[6];
    v->day = p[7];
    v->hour = p[8];
    v->min = p[9];
    v->sec = p[10] + i4(p + 16) * 1e-9;
    v->fix_type = p[20];
    v->num_sv = p[23];
    v->pos.lon_deg = i4(p + 24) * 1e-7;
    v->pos.lat_deg = i4(p + 28) * 1e-7;
    v->pos.height_m = i4(p + 32) / 1000.0;
    v->h_acc_m = u4(p + 40) / 1000.0;
    v->v_acc_m = u4(p + 44) / 1000.0;
    v->speed_mps = i4(p + 60) / 1000.0;
    v->heading_deg = i4(p + 64) * 1e-5;
    v->pdop = u2(p + 76) / 100.0;
    return 0;
}

/* ---- the receiver ---- */

void sqrx_receiver_init(SqrxReceiver *r) {
    memset(r, 0, sizeof *r);
    sqrx_framer_init(&r->framer);
    sqrx_nmea_init(&r->nmea);
}

static int band_of(int sig) {
    switch (sig) {
    case SIG_L1CA: return 0;
    case SIG_L2CL:
    case SIG_L2CM: return 1;
    case SIG_L5I:
    case SIG_L5Q: return 2;
    default: return -1;
    }
}

static SqgObservation *obs_for(SqgEpoch *ep, int prn) {
    for (int i = 0; i < ep->n_obs; i++)
        if (ep->obs[i].prn == prn) return &ep->obs[i];
    if (ep->n_obs >= SQG_MAX_SATS) return NULL;
    SqgObservation *o = &ep->obs[ep->n_obs++];
    memset(o, 0, sizeof *o);
    o->prn = prn;
    return o;
}

static int rawx(SqrxReceiver *r, const unsigned char *p, int len) {
    if (len < 16) return 0;
    int n = p[11];
    if (len < 16 + 32 * n) return 0;
    SqgEpoch ep;
    memset(&ep, 0, sizeof ep);
    ep.t_receiver = r8(p);
    int week = (int)u2(p + 8);
    int got_l1[SQG_MAX_SATS + 1] = {0};
    for (int k = 0; k < n; k++) {
        const unsigned char *m = p + 16 + 32 * k;
        int gnss = m[20], prn = m[21], band = band_of(m[22]), trk = m[30];
        if (gnss != 0 || prn < 1 || prn > 32 || band < 0 || !(trk & 1)) continue;
        double pr = r8(m), cp = r8(m + 8);
        int cp_ok = (trk & 2) && (trk & 4) && cp != 0; /* valid, half cycle resolved */
        unsigned lock = u2(m + 24);
        /* a lock time that went back (or a first one of 0) is a slip */
        int slip = cp_ok && (lock < r->lock_ms[prn - 1][band] || (lock == 0 && r->seen[prn - 1][band]));
        r->lock_ms[prn - 1][band] = lock;
        r->seen[prn - 1][band] = 1;
        if (!(pr > 1e6 && pr < 1e9)) continue;
        SqgObservation *o = obs_for(&ep, prn);
        if (!o) continue;
        if (slip) o->slip = 1;
        if (band == 0) {
            o->pseudorange_m = pr;
            o->snr_dbhz = m[26];
            o->valid = 1;
            o->carrier_cyc = cp_ok ? cp : 0;
            o->has_carrier = cp_ok;
            got_l1[prn] = 1;
        } else if (band == 1 && cp_ok) {
            o->pseudorange2_m = pr;
            o->carrier2_cyc = cp;
            o->has_l2 = 1;
        } else if (band == 2) {
            o->pseudorange5_m = pr;
            o->has_l5 = 1;
            o->carrier5_cyc = cp_ok ? cp : 0;
            o->has_l5_carrier = cp_ok;
        }
    }
    /* every observation needs L1 C/A: drop those without it */
    int k = 0;
    for (int i = 0; i < ep.n_obs; i++)
        if (got_l1[ep.obs[i].prn]) ep.obs[k++] = ep.obs[i];
    ep.n_obs = k;
    /* L2 needs the L1 carrier too (as RINEX reading does) */
    for (int i = 0; i < ep.n_obs; i++)
        if (!ep.obs[i].has_carrier) ep.obs[i].has_l2 = 0;
    r->epoch = ep;
    r->week = week;
    r->n_epochs++;
    return SQRX_EV_EPOCH;
}

static int sfrbx(SqrxReceiver *r, const unsigned char *p, int len) {
    if (len < 8) return 0;
    int gnss = p[0], prn = p[1], nw = p[4];
    if (gnss != 0 || prn < 1 || prn > 32 || nw != 10 || len < 8 + 40) return 0;
    SqrxSubframe w;
    for (int i = 0; i < 10; i++) w[i] = u4(p + 8 + 4 * i);
    if (getu(w, 0, 8) != 0x8B) return 0;
    int id = (int)getu(w, 43, 3);
    if (id < 1 || id > 3) return 0; /* almanac pages: not used */
    memcpy(r->sub[prn - 1][id - 1], w, sizeof w);
    r->sub_have[prn - 1] |= 1 << (id - 1);
    if (r->sub_have[prn - 1] != 7) return 0;
    SqgEphemeris e;
    if (sqrx_lnav_decode((const SqrxSubframe *)r->sub[prn - 1], r->week, &e)) return 0;
    e.prn = prn;
    int is_new = !r->have_eph[prn - 1] || memcmp(&e, &r->eph[prn - 1], sizeof e) != 0;
    r->eph[prn - 1] = e;
    r->have_eph[prn - 1] = 1;
    if (!is_new) return 0;
    r->n_eph_decoded++;
    return SQRX_EV_EPHEMERIS;
}

int sqrx_receiver_byte(SqrxReceiver *r, unsigned char b) {
    SqrxFrame f;
    if (!sqrx_framer_byte(&r->framer, b, &f)) return 0;
    if (f.kind == SQRX_FRAME_NMEA) return sqrx_nmea_parse(&r->nmea, f.nmea) > 0 ? SQRX_EV_NMEA : 0;
    if (f.cls == SQRX_UBX_RXM && f.id == SQRX_UBX_RXM_RAWX) return rawx(r, f.payload, f.len);
    if (f.cls == SQRX_UBX_RXM && f.id == SQRX_UBX_RXM_SFRBX) return sfrbx(r, f.payload, f.len);
    if (f.cls == SQRX_UBX_NAV && f.id == SQRX_UBX_NAV_PVT) {
        if (sqrx_ubx_navpvt_read(f.payload, f.len, &r->navpvt)) return 0;
        r->have_navpvt = 1;
        return SQRX_EV_NAVPVT;
    }
    return 0;
}

int sqrx_receiver_ephemerides(const SqrxReceiver *r, SqgEphemeris *out, int cap) {
    int n = 0;
    for (int i = 0; i < 32 && n < cap; i++)
        if (r->have_eph[i] && r->eph[i].health == 0) out[n++] = r->eph[i];
    return n;
}

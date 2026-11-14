/*
 * sqrecv: a GPS receiver's own output, live over a serial port or replayed
 * from a file, for SquirrelGNSS (original code, written from the public
 * NMEA 0183 sentence layouts, the u-blox UBX protocol description and the
 * IS-GPS-200 navigation message).
 *
 * One byte stream may mix NMEA sentences and UBX messages, as receivers
 * send them. From NMEA it keeps the receiver's fix (GGA, RMC, GSA) and the
 * satellites in view (GSV). From UBX it takes raw measurements (RXM-RAWX:
 * GPS L1 C/A, L2C and L5 code and carrier, cycle slips from the lock time),
 * the broadcast navigation message (RXM-SFRBX subframes 1 to 3, decoded to
 * ephemerides) and the receiver's own solution (NAV-PVT), so the stream can
 * be solved here and compared with the receiver.
 *
 * Writers build the same messages, for configuring a receiver (CFG-MSG)
 * and for synthetic test streams. C11, no allocation; the serial port is
 * POSIX (termios) or Win32 (CreateFile / DCB) on Windows.
 */
#ifndef SQRECV_H
#define SQRECV_H

#include <stddef.h>
#include <stdint.h>

#include "sqgnss.h"

#define SQRX_MAX_PAYLOAD 2048 /* longer UBX messages are dropped */
#define SQRX_MAX_NMEA 120     /* 82 by the standard; some receivers send more */
#define SQRX_MAX_VIEW 64

/* ---- framing ---- */

enum { SQRX_FRAME_NMEA = 1, SQRX_FRAME_UBX = 2 };

typedef struct {
    int kind;                     /* SQRX_FRAME_* */
    const char *nmea;             /* the sentence from '$' to the checksum, NUL-terminated */
    int cls, id;                  /* UBX class and id */
    const unsigned char *payload; /* UBX payload */
    int len;
} SqrxFrame;

typedef struct {
    int state, need;
    unsigned char buf[SQRX_MAX_PAYLOAD + 8];
    int len;
    unsigned char ck_a, ck_b;
    /* what went by */
    long bytes, nmea_ok, nmea_bad, ubx_ok, ubx_bad, junk;
} SqrxFramer;

void sqrx_framer_init(SqrxFramer *f);
/* One byte in: 1 when it completes a good frame (in *out, valid until the
 * next call), else 0. Bad checksums, overlong frames and bytes between
 * frames are counted and skipped. */
int sqrx_framer_byte(SqrxFramer *f, unsigned char b, SqrxFrame *out);

/* ---- NMEA 0183 ---- */

typedef struct {
    char sys; /* 'P' GPS, 'L' GLONASS, 'A' Galileo, 'B' BeiDou, 'Q' QZSS, 'N' other */
    int prn;
    double el_deg, az_deg, snr_dbhz; /* snr 0 when not tracked */
} SqrxSatView;

typedef struct {
    /* GGA */
    double utc_s; /* seconds of the UTC day */
    int have_pos;
    SqgGeodetic pos; /* height above the ellipsoid (MSL + geoid separation) */
    double msl_m, geoid_sep_m;
    int quality; /* 0 none, 1 GPS, 2 DGPS, 4 RTK fixed, 5 RTK float, 6 dead reckoning */
    int sats_used;
    double hdop;
    /* RMC */
    int have_date, year, month, day;
    int rmc_valid;
    double speed_mps, course_deg;
    /* GSA */
    int fix_mode; /* 1 none, 2 2-D, 3 3-D */
    int used_prn[12], n_used_prn;
    double pdop, vdop;
    /* GSV: the last complete set of each system */
    SqrxSatView view[SQRX_MAX_VIEW];
    int n_view;
    SqrxSatView gsv[SQRX_MAX_VIEW]; /* the set being gathered */
    int gsv_n, gsv_msgs, gsv_next;
    char gsv_sys;
} SqrxNmea;

enum { SQRX_NMEA_GGA = 1, SQRX_NMEA_RMC, SQRX_NMEA_GSA, SQRX_NMEA_GSV, SQRX_NMEA_OTHER };

void sqrx_nmea_init(SqrxNmea *n);
/* 1 when "$...*HH" has the right checksum */
int sqrx_nmea_checksum_ok(const char *sentence);
/* Read one sentence (any talker). Returns SQRX_NMEA_*, or -1 when it is
 * damaged (bad checksum, missing fields, numbers that are not). */
int sqrx_nmea_parse(SqrxNmea *n, const char *sentence);

/* Sentence writers: "$GP...*HH\r\n" into out; the length, or 0 when cap is
 * too small. GSV writes as many sentences as the satellites need. */
size_t sqrx_nmea_gga(char *out, size_t cap, double utc_s, SqgGeodetic pos, int quality, int sats, double hdop);
size_t sqrx_nmea_rmc(char *out, size_t cap, double utc_s, int year, int month, int day, SqgGeodetic pos,
                     double speed_mps, double course_deg);
size_t sqrx_nmea_gsv(char *out, size_t cap, const SqrxSatView *sats, int n);

/* ---- the GPS navigation message (LNAV) ---- */

/* A subframe as receivers deliver it: ten 30-bit words, each right-aligned
 * in 32 bits with its 24 data bits at 29..6 (the 6 parity bits below are
 * not checked: the receiver has done that). */
typedef uint32_t SqrxSubframe[10];

/* Subframes 1, 2 and 3 of one ephemeris into *out. week_hint is the full
 * GPS week near the data (to resolve the 10-bit week), or 0 for the
 * 2019-2038 era. Returns 0, or -1 when they are not 1, 2 and 3 or their
 * issue numbers (IODC, IODE) disagree. */
int sqrx_lnav_decode(const SqrxSubframe sf[3], int week_hint, SqgEphemeris *out);
/* The other way, to the message's resolution (iode 0..255; tow_s the GPS
 * second of the week at which subframe 1 starts, a multiple of 6). toe and
 * toc must be multiples of 16 s; angles are wrapped. Returns 0, or -1 when
 * a value does not fit its field. */
int sqrx_lnav_encode(const SqgEphemeris *e, int iode, double tow_s, SqrxSubframe sf[3]);

/* ---- UBX ---- */

#define SQRX_UBX_NAV 0x01
#define SQRX_UBX_RXM 0x02
#define SQRX_UBX_CFG 0x06
#define SQRX_UBX_NAV_PVT 0x07
#define SQRX_UBX_RXM_SFRBX 0x13
#define SQRX_UBX_RXM_RAWX 0x15
#define SQRX_UBX_CFG_MSG 0x01

typedef struct {
    double tow_s; /* iTOW */
    int year, month, day, hour, min;
    double sec;
    int fix_type; /* 0 none, 2 2-D, 3 3-D, ... */
    int num_sv;
    SqgGeodetic pos; /* height above the ellipsoid */
    double h_acc_m, v_acc_m, speed_mps, heading_deg, pdop;
} SqrxNavPvt;

/* A whole UBX message (sync, header, payload, checksum) into out: its
 * length, or 0 when cap is too small. */
size_t sqrx_ubx_frame(unsigned char *out, size_t cap, int cls, int id, const unsigned char *payload, int len);
/* CFG-MSG: send cls/id on the current port every `rate` solutions (0 off) */
size_t sqrx_ubx_cfg_msg(unsigned char *out, size_t cap, int cls, int id, int rate);
/* RXM-RAWX of a GPS epoch: L1 C/A, and L2 CL and L5 Q where the
 * observations have them; a slip gives a lock time of 0. */
size_t sqrx_ubx_rawx(unsigned char *out, size_t cap, const SqgEpoch *ep, int week);
size_t sqrx_ubx_sfrbx(unsigned char *out, size_t cap, int prn, const SqrxSubframe words);
size_t sqrx_ubx_navpvt(unsigned char *out, size_t cap, const SqrxNavPvt *p);
int sqrx_ubx_navpvt_read(const unsigned char *p, int len, SqrxNavPvt *out);

/* ---- a receiver ---- */

enum { SQRX_EV_NMEA = 1, SQRX_EV_EPOCH = 2, SQRX_EV_EPHEMERIS = 4, SQRX_EV_NAVPVT = 8 };

typedef struct {
    SqrxFramer framer;
    SqrxNmea nmea;
    /* the navigation message: subframes gathered per PRN, ephemerides made */
    SqrxSubframe sub[32][3];
    int sub_have[32]; /* bit k: subframe k+1 */
    SqgEphemeris eph[32];
    int have_eph[32];
    int n_eph_decoded;
    /* the latest raw epoch */
    SqgEpoch epoch;
    int week;
    unsigned lock_ms[32][3]; /* last lock time per PRN and band (L1, L2, L5) */
    int seen[32][3];
    long n_epochs;
    SqrxNavPvt navpvt;
    int have_navpvt;
} SqrxReceiver;

void sqrx_receiver_init(SqrxReceiver *r);
/* One byte of the stream: what it completed (SQRX_EV_* bits): a sentence
 * read into r->nmea, a raw epoch in r->epoch (with r->week), a new
 * ephemeris, the receiver's own solution in r->navpvt. */
int sqrx_receiver_byte(SqrxReceiver *r, unsigned char b);
/* The ephemerides decoded so far, one per PRN; returns how many. */
int sqrx_receiver_ephemerides(const SqrxReceiver *r, SqgEphemeris *out, int cap);

/* ---- the serial port (sqserial.c) ---- */

/* Open a serial device raw at baud (8N1, no flow control), or any other
 * file as it is (a recorded stream; "-" is standard input). Returns a file
 * descriptor, or -1 with a message in err. */
int sqrx_serial_open(const char *path, int baud, int write, char *err, size_t cap);
long sqrx_serial_read(int fd, unsigned char *buf, size_t cap); /* bytes, 0 at the end, -1 on error */
int sqrx_serial_write(int fd, const unsigned char *buf, size_t n);
void sqrx_serial_close(int fd);

#endif

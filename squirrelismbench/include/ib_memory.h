#ifndef IB_MEMORY_H
#define IB_MEMORY_H

#include "ib_iface.h"

#define IB_SPD_MAX 256
#define IB_MEM_BANK_MAX 65536

typedef struct {
    char part[IB_NAME_LEN];
    int density_kib;
    int banks;
    int spd_i2c_addr; /* typically 0x50 */
    int spd_len;
    unsigned char spd[IB_SPD_MAX];
    int dq_width;
    int pattern_bytes; /* synthetic bank size for CI */
    unsigned int lcg_seed; /* NR Ch. 7.1 class seed for random patterns */
} IbMemoryIface;

typedef struct {
    int ok;
    int npass;
    int nfail;
    char summary[IB_MSG_LEN];
} IbMemTestResult;

int ib_memory_parse(IbMemoryIface *m, const char *text, char *err, int err_cap);

/* Soft SPD contract: byte[n-1] == XOR of bytes[0..n-2] (n = spd_len). */
int ib_memory_spd_checksum_ok(const IbMemoryIface *m);

/*
 * Soft↔ISM USB-CDC SPD framing (Soft #203 / S8.4b″):
 *   ISM SPD: <hh> <hh> ...   (hex bytes; last = XOR of prior)
 * Optional leading keywords are ignored after the prefix:
 *   ISM SPD: addr=0x50 23 10 ...
 * Returns 0 and fills spd/spd_len on success; -1 on refuse/parse error.
 */
int ib_memory_spd_parse_line(const char *line, unsigned char *spd, int *spd_len,
                             int spd_cap, char *err, int err_cap);

/*
 * Scan a USB-CDC transcript (live ACM or Soft stub .sqspd) for the first
 * ISM SPD: line and apply it onto m (iface fields otherwise unchanged).
 * Soft XOR contract must hold. Returns 0 on ok.
 */
int ib_memory_spd_apply_cdc_text(IbMemoryIface *m, const char *text, char *err,
                                 int err_cap);

/* Read path (file stub or /dev/ttyACM*) and apply SPD framing onto m. */
int ib_memory_spd_apply_cdc_path(IbMemoryIface *m, const char *path, char *err,
                                 int err_cap);

/*
 * Synthetic DRAM bank self-test (fixture mode until live PSRAM/I2C):
 * walking-1, walking-0, checkerboard, and Park–Miller LCG fill/verify
 * (NR Ch. 7.1 class of generator — original Soft code, not an NR listing).
 */
int ib_memory_selftest(const IbMemoryIface *m, IbMemTestResult *res);

#endif

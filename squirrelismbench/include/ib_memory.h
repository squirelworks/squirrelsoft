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
 * Synthetic DRAM bank self-test (fixture mode until live PSRAM/I2C):
 * walking-1, walking-0, checkerboard, and Park–Miller LCG fill/verify
 * (NR Ch. 7.1 class of generator — original Soft code, not an NR listing).
 */
int ib_memory_selftest(const IbMemoryIface *m, IbMemTestResult *res);

#endif

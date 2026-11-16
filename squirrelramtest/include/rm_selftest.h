#ifndef RM_SELFTEST_H
#define RM_SELFTEST_H

#include "rm_iface.h"

typedef struct {
    int ok;
    int npass;
    int nfail;
    char summary[RM_MSG_LEN];
} RmTestResult;

/*
 * Fixture-mode DRAM bank self-test until live PSRAM/I2C:
 * SPD XOR, const 0x00/0xff, walking-1/0, checkerboard, Park–Miller LCG
 * fill/verify (NR Ch. 7.1 class — Soft original, not an NR listing).
 */
int rm_selftest_run(const RmIface *m, RmTestResult *res);

#endif

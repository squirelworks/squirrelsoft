/*
 * Soft S8.4b — Soft storage / lab-media Soft bench (Soft test software).
 * Soft-proposed SD/eMMC Soft fixture until Eric publishes machine-readable SoT.
 * Soft bank patterns reuse Park–Miller LCG (NR Ch. 7.1 class; Soft original —
 * no NR listings). Live removable-media / CDC paths deferred.
 */
#ifndef IB_STORAGE_H
#define IB_STORAGE_H

#include "ib_iface.h"

#define IB_STOR_BANK_MAX 65536

typedef struct {
    char media[32]; /* sd_card / emmc / flash */
    char part[IB_NAME_LEN];
    int block_bytes;
    int capacity_kib;
    int pattern_bytes;
    unsigned int lcg_seed;
    unsigned int erase_value; /* byte after Soft erase */
} IbStorageIface;

typedef struct {
    int ok;
    int npass;
    int nfail;
    unsigned int soft_crc;
    char summary[IB_MSG_LEN];
} IbStorageTestResult;

int ib_storage_parse(IbStorageIface *s, const char *text, char *err, int err_cap);

/* Soft erase / program / verify Soft self-test (no live media). */
int ib_storage_selftest(const IbStorageIface *s, IbStorageTestResult *res);

#endif

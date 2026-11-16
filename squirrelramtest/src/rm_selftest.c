#include "rm_selftest.h"
#include "rm_spd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Park–Miller "minimal standard" multiplicative congruential generator.
 * Cited as the NR Ch. 7.1 class of portable LCG (a=16807, m=2^31-1).
 * Original Soft implementation — do not paste Numerical Recipes listings.
 */
static unsigned int rm_park_miller(unsigned int *state)
{
    const unsigned int a = 16807u;
    const unsigned int m = 2147483647u;
    unsigned long long prod = (unsigned long long)(*state) * (unsigned long long)a;
    *state = (unsigned int)(prod % m);
    if (*state == 0)
        *state = 1;
    return *state;
}

static unsigned char rm_lcg_byte(unsigned int *state)
{
    return (unsigned char)(rm_park_miller(state) & 0xffu);
}

static int fail_pat(RmTestResult *res)
{
    res->nfail++;
    res->ok = 0;
    return -1;
}

static int run_const(unsigned char *bank, int n, unsigned char v, RmTestResult *res)
{
    int i;
    for (i = 0; i < n; i++)
        bank[i] = v;
    for (i = 0; i < n; i++) {
        if (bank[i] != v)
            return fail_pat(res);
    }
    res->npass++;
    return 0;
}

static int run_walking(unsigned char *bank, int n, int ones, RmTestResult *res)
{
    int bit, i;
    for (bit = 0; bit < 8; bit++) {
        unsigned char mask = (unsigned char)(1u << bit);
        for (i = 0; i < n; i++)
            bank[i] = ones ? mask : (unsigned char)~mask;
        for (i = 0; i < n; i++) {
            unsigned char expect = ones ? mask : (unsigned char)~mask;
            if (bank[i] != expect)
                return fail_pat(res);
        }
        res->npass++;
    }
    return 0;
}

static int run_checker(unsigned char *bank, int n, RmTestResult *res)
{
    int i;
    for (i = 0; i < n; i++)
        bank[i] = (i & 1) ? 0xAAu : 0x55u;
    for (i = 0; i < n; i++) {
        unsigned char expect = (i & 1) ? 0xAAu : 0x55u;
        if (bank[i] != expect)
            return fail_pat(res);
    }
    res->npass++;
    for (i = 0; i < n; i++)
        bank[i] = (i & 1) ? 0x55u : 0xAAu;
    for (i = 0; i < n; i++) {
        unsigned char expect = (i & 1) ? 0x55u : 0xAAu;
        if (bank[i] != expect)
            return fail_pat(res);
    }
    res->npass++;
    return 0;
}

static int run_lcg(unsigned char *bank, int n, unsigned int seed, RmTestResult *res)
{
    unsigned int s = seed ? seed : 1u;
    int i;
    for (i = 0; i < n; i++)
        bank[i] = rm_lcg_byte(&s);
    s = seed ? seed : 1u;
    for (i = 0; i < n; i++) {
        unsigned char expect = rm_lcg_byte(&s);
        if (bank[i] != expect)
            return fail_pat(res);
    }
    res->npass++;
    return 0;
}

int rm_selftest_run(const RmIface *m, RmTestResult *res)
{
    unsigned char *bank;
    int n;

    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!rm_spd_checksum_ok(m)) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary,
                 "ramtest part=%s spd_xor FAIL → FAIL", m->part);
        return 0;
    }
    res->npass++; /* SPD XOR contract */

    n = m->pattern_bytes;
    bank = malloc((size_t)n);
    if (!bank) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "ramtest oom → FAIL");
        return 0;
    }

    if (run_const(bank, n, 0x00, res) != 0)
        goto done;
    if (run_const(bank, n, 0xff, res) != 0)
        goto done;
    if (run_walking(bank, n, 1, res) != 0)
        goto done;
    if (run_walking(bank, n, 0, res) != 0)
        goto done;
    if (run_checker(bank, n, res) != 0)
        goto done;
    if (run_lcg(bank, n, m->lcg_seed, res) != 0)
        goto done;

done:
    free(bank);
    snprintf(res->summary, sizeof res->summary,
             "ramtest part=%s banks=%d spd_len=%d patterns_ok=%d fail=%d "
             "(NR Ch.7.1 LCG oracle) → %s",
             m->part, m->banks, m->spd_len, res->npass, res->nfail,
             res->ok ? "PASS" : "FAIL");
    return 0;
}

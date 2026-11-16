#include "rm_spd.h"

#include <stdio.h>

int rm_spd_checksum_ok(const RmIface *m)
{
    int i;
    unsigned char x = 0;

    if (!m || m->spd_len < 2 || m->spd_len > RM_SPD_MAX)
        return 0;
    for (i = 0; i < m->spd_len - 1; i++)
        x ^= m->spd[i];
    return x == m->spd[m->spd_len - 1];
}

int rm_spd_print(const RmIface *m, char *buf, int buflen)
{
    int i, n = 0;

    if (!m || !buf || buflen < 32)
        return -1;
    n += snprintf(buf + n, (size_t)(buflen - n),
                  "part=%s density_kib=%d banks=%d dq=%d\n", m->part,
                  m->density_kib, m->banks, m->dq_width);
    if (n < 0 || n >= buflen)
        return -1;
    n += snprintf(buf + n, (size_t)(buflen - n),
                  "spd_i2c=0x%02x spd_len=%d xor=%s pattern_bytes=%d\n",
                  m->spd_i2c_addr, m->spd_len,
                  rm_spd_checksum_ok(m) ? "ok" : "FAIL", m->pattern_bytes);
    if (n < 0 || n >= buflen)
        return -1;
    n += snprintf(buf + n, (size_t)(buflen - n), "spd:");
    if (n < 0 || n >= buflen)
        return -1;
    for (i = 0; i < m->spd_len; i++) {
        n += snprintf(buf + n, (size_t)(buflen - n), " %02x", m->spd[i]);
        if (n < 0 || n >= buflen)
            return -1;
    }
    n += snprintf(buf + n, (size_t)(buflen - n), "\n");
    return (n < 0 || n >= buflen) ? -1 : 0;
}

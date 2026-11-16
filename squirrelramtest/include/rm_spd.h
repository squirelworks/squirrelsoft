#ifndef RM_SPD_H
#define RM_SPD_H

#include "rm_iface.h"

/* Soft SPD contract: byte[n-1] == XOR of bytes[0..n-2] (n = spd_len). */
int rm_spd_checksum_ok(const RmIface *m);

/* Print SPD summary into buf. Returns 0 on success. */
int rm_spd_print(const RmIface *m, char *buf, int buflen);

#endif

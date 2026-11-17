#ifndef RM_SPD_H
#define RM_SPD_H

#include "rm_iface.h"

/* Soft SPD contract: byte[n-1] == XOR of bytes[0..n-2] (n = spd_len). */
int rm_spd_checksum_ok(const RmIface *m);

/* Print SPD summary into buf. Returns 0 on success. */
int rm_spd_print(const RmIface *m, char *buf, int buflen);

/*
 * Soft↔ISM USB-CDC framing (Soft #203): `ISM SPD: [addr=…] <hh>+`
 * See squirrelismbench/docs/spd-cdc.md. Returns 0 on ok.
 */
int rm_spd_parse_line(const char *line, unsigned char *spd, int *spd_len,
                      int spd_cap, char *err, int err_cap);
int rm_spd_apply_cdc_path(RmIface *m, const char *path, char *err, int err_cap);

#endif

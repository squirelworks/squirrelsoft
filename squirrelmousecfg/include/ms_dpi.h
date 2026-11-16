#ifndef MS_DPI_H
#define MS_DPI_H

#include "ms_iface.h"

/* Print DPI profile name, steps, and active selection. */
int ms_dpi_print(const MsIface *m, char *out, int outlen);

/* Set active DPI. If cpi is not in steps, append it when room remains. */
int ms_dpi_set(MsIface *m, int cpi);

/* True if cpi is listed in dpi_steps. */
int ms_dpi_has_step(const MsIface *m, int cpi);

#endif

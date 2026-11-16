#include "ms_dpi.h"

#include <stdio.h>
#include <string.h>

int ms_dpi_has_step(const MsIface *m, int cpi)
{
    int i;
    if (!m || cpi <= 0)
        return 0;
    for (i = 0; i < m->ndpi; i++) {
        if (m->dpi_steps[i] == cpi)
            return 1;
    }
    return 0;
}

int ms_dpi_set(MsIface *m, int cpi)
{
    if (!m || cpi <= 0)
        return -1;
    if (!ms_dpi_has_step(m, cpi)) {
        if (m->ndpi >= MS_MAX_DPI_STEPS)
            return -1;
        m->dpi_steps[m->ndpi++] = cpi;
    }
    m->active_dpi = cpi;
    return 0;
}

int ms_dpi_print(const MsIface *m, char *out, int outlen)
{
    int i, n = 0;
    if (!m || !out || outlen < 8)
        return -1;
    out[0] = 0;
    n += snprintf(out + n, outlen - n,
                  "dpi profile=%s active=%d default=%d report_hz=%d\nsteps:",
                  m->dpi_profile, m->active_dpi, m->cpi_default, m->report_hz);
    for (i = 0; i < m->ndpi; i++) {
        if (n >= outlen - 16)
            return -1;
        n += snprintf(out + n, outlen - n, " %d", m->dpi_steps[i]);
        if (m->dpi_steps[i] == m->active_dpi)
            n += snprintf(out + n, outlen - n, "*");
    }
    n += snprintf(out + n, outlen - n, "\n");
    return 0;
}

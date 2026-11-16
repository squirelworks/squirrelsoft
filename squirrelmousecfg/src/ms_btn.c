#include "ms_btn.h"

#include <stdio.h>
#include <string.h>

const char *ms_btn_action(const MsIface *m, const char *physical)
{
    int i;
    if (!m || !physical)
        return NULL;
    for (i = 0; i < m->nmap; i++) {
        if (strcmp(m->map[i].physical, physical) == 0)
            return m->map[i].action;
    }
    return NULL;
}

int ms_btn_remap(MsIface *m, const char *physical, const char *action)
{
    int i;
    if (!m || !physical || !action || !physical[0] || !action[0])
        return -1;
    for (i = 0; i < m->nmap; i++) {
        if (strcmp(m->map[i].physical, physical) == 0) {
            snprintf(m->map[i].action, sizeof(m->map[i].action), "%s", action);
            return 0;
        }
    }
    if (m->nmap >= MS_MAX_BUTTONS)
        return -1;
    snprintf(m->map[m->nmap].physical, sizeof(m->map[0].physical), "%s", physical);
    snprintf(m->map[m->nmap].action, sizeof(m->map[0].action), "%s", action);
    m->nmap++;
    if (m->buttons < m->nmap)
        m->buttons = m->nmap;
    return 0;
}

int ms_btn_print(const MsIface *m, char *out, int outlen)
{
    int i, n = 0;
    if (!m || !out || outlen < 8)
        return -1;
    out[0] = 0;
    n += snprintf(out + n, outlen - n, "buttons count=%d map=%d sensor=%s scroll=%s\n",
                  m->buttons, m->nmap, m->sensor, m->scroll);
    for (i = 0; i < m->nmap; i++) {
        if (n >= outlen - 48)
            return -1;
        n += snprintf(out + n, outlen - n, "  %s -> %s\n", m->map[i].physical,
                      m->map[i].action);
    }
    return 0;
}

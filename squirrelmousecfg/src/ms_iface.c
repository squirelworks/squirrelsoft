#include "ms_iface.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip(char *s)
{
    size_t n;
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
}

int ms_iface_load(const char *path, MsIface *out, char *err, int errlen)
{
    FILE *f;
    char line[256];

    if (!path || !out) {
        if (err && errlen > 0)
            snprintf(err, errlen, "null arg");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->buttons = 3;
    out->cpi_default = 800;
    out->report_hz = 125;
    snprintf(out->sensor, sizeof(out->sensor), "optical_stub");
    snprintf(out->scroll, sizeof(out->scroll), "vertical");
    snprintf(out->hid_report, sizeof(out->hid_report), "boot_mouse");
    snprintf(out->dpi_profile, sizeof(out->dpi_profile), "default");
    snprintf(out->path, sizeof(out->path), "%s", path);

    f = fopen(path, "r");
    if (!f) {
        if (err && errlen > 0)
            snprintf(err, errlen, "open %s", path);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        strip(p);
        if (p[0] == 0 || p[0] == '#')
            continue;
        if (strncmp(p, "ISM-HID", 7) == 0)
            continue;
        if (strncmp(p, "SQMS1", 5) == 0)
            continue;
        if (strncmp(p, "sensor:", 7) == 0) {
            char *v = p + 7;
            strip(v);
            snprintf(out->sensor, sizeof(out->sensor), "%s", v);
            continue;
        }
        if (strncmp(p, "buttons:", 8) == 0) {
            out->buttons = atoi(p + 8);
            continue;
        }
        if (strncmp(p, "scroll:", 7) == 0) {
            char *v = p + 7;
            strip(v);
            snprintf(out->scroll, sizeof(out->scroll), "%s", v);
            continue;
        }
        if (strncmp(p, "hid_report:", 11) == 0) {
            char *v = p + 11;
            strip(v);
            snprintf(out->hid_report, sizeof(out->hid_report), "%s", v);
            continue;
        }
        if (strncmp(p, "cpi_default:", 12) == 0) {
            out->cpi_default = atoi(p + 12);
            continue;
        }
        if (strncmp(p, "report_hz:", 10) == 0) {
            out->report_hz = atoi(p + 10);
            continue;
        }
        if (strncmp(p, "dpi_profile:", 12) == 0) {
            char *v = p + 12;
            strip(v);
            snprintf(out->dpi_profile, sizeof(out->dpi_profile), "%s", v);
            continue;
        }
        if (strncmp(p, "active_dpi:", 11) == 0) {
            out->active_dpi = atoi(p + 11);
            continue;
        }
        if (strncmp(p, "dpi_step:", 9) == 0) {
            int cpi = atoi(p + 9);
            if (cpi > 0 && out->ndpi < MS_MAX_DPI_STEPS)
                out->dpi_steps[out->ndpi++] = cpi;
            continue;
        }
        if (strncmp(p, "button ", 7) == 0) {
            char phys[MS_NAME_LEN];
            char *colon;
            char *act;
            if (out->nmap >= MS_MAX_BUTTONS)
                continue;
            colon = strchr(p + 7, ':');
            if (!colon)
                continue;
            {
                size_t nlen = (size_t)(colon - (p + 7));
                if (nlen == 0 || nlen >= sizeof(phys))
                    continue;
                memcpy(phys, p + 7, nlen);
                phys[nlen] = 0;
                strip(phys);
            }
            act = colon + 1;
            strip(act);
            if (phys[0] && act[0]) {
                MsButton *b = &out->map[out->nmap++];
                snprintf(b->physical, sizeof(b->physical), "%s", phys);
                snprintf(b->action, sizeof(b->action), "%s", act);
            }
            continue;
        }
    }
    fclose(f);

    if (out->active_dpi <= 0)
        out->active_dpi = out->cpi_default;
    if (out->ndpi == 0) {
        out->dpi_steps[0] = out->cpi_default;
        out->ndpi = 1;
    }
    if (out->nmap == 0) {
        /* Default 3-button identity map when fixture omits button lines. */
        snprintf(out->map[0].physical, sizeof(out->map[0].physical), "left");
        snprintf(out->map[0].action, sizeof(out->map[0].action), "left");
        snprintf(out->map[1].physical, sizeof(out->map[1].physical), "right");
        snprintf(out->map[1].action, sizeof(out->map[1].action), "right");
        snprintf(out->map[2].physical, sizeof(out->map[2].physical), "middle");
        snprintf(out->map[2].action, sizeof(out->map[2].action), "middle");
        out->nmap = 3;
    }
    if (out->buttons < 1 || out->cpi_default <= 0) {
        if (err && errlen > 0)
            snprintf(err, errlen, "incomplete iface");
        return -1;
    }
    return 0;
}

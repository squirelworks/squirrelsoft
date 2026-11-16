#include "ms_cfg.h"

#include <stdio.h>
#include <string.h>

int ms_cfg_from_iface(const MsIface *m, MsCfg *cfg)
{
    if (!m || !cfg)
        return -1;
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->magic, sizeof(cfg->magic), "%s", MS_CFG_MAGIC);
    snprintf(cfg->sensor, sizeof(cfg->sensor), "%s", m->sensor);
    snprintf(cfg->scroll, sizeof(cfg->scroll), "%s", m->scroll);
    snprintf(cfg->hid_report, sizeof(cfg->hid_report), "%s", m->hid_report);
    snprintf(cfg->dpi_profile, sizeof(cfg->dpi_profile), "%s", m->dpi_profile);
    cfg->cpi_default = m->cpi_default;
    cfg->report_hz = m->report_hz;
    cfg->buttons = m->buttons;
    cfg->ndpi = m->ndpi;
    memcpy(cfg->dpi_steps, m->dpi_steps, sizeof(int) * (size_t)m->ndpi);
    cfg->active_dpi = m->active_dpi;
    cfg->nmap = m->nmap;
    memcpy(cfg->map, m->map, sizeof(MsButton) * (size_t)m->nmap);
    return 0;
}

int ms_cfg_write(const char *path, const MsCfg *cfg)
{
    FILE *f;
    int i;
    if (!path || !cfg)
        return -1;
    f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "%s\n", MS_CFG_MAGIC);
    fprintf(f, "sensor: %s\n", cfg->sensor);
    fprintf(f, "buttons: %d\n", cfg->buttons);
    fprintf(f, "scroll: %s\n", cfg->scroll);
    fprintf(f, "hid_report: %s\n", cfg->hid_report);
    fprintf(f, "cpi_default: %d\n", cfg->cpi_default);
    fprintf(f, "report_hz: %d\n", cfg->report_hz);
    fprintf(f, "dpi_profile: %s\n", cfg->dpi_profile);
    fprintf(f, "active_dpi: %d\n", cfg->active_dpi);
    for (i = 0; i < cfg->ndpi; i++)
        fprintf(f, "dpi_step: %d\n", cfg->dpi_steps[i]);
    for (i = 0; i < cfg->nmap; i++)
        fprintf(f, "button %s: %s\n", cfg->map[i].physical, cfg->map[i].action);
    fclose(f);
    return 0;
}

int ms_cfg_read(const char *path, MsCfg *cfg, char *err, int errlen)
{
    MsIface tmp;
    FILE *f;
    char line[64];

    f = fopen(path, "r");
    if (!f) {
        if (err && errlen > 0)
            snprintf(err, errlen, "open %s", path);
        return -1;
    }
    if (!fgets(line, sizeof(line), f) || strncmp(line, MS_CFG_MAGIC, 5) != 0) {
        fclose(f);
        if (err && errlen > 0)
            snprintf(err, errlen, "bad magic");
        return -1;
    }
    fclose(f);

    if (ms_iface_load(path, &tmp, err, errlen) != 0)
        return -1;
    return ms_cfg_from_iface(&tmp, cfg);
}

int ms_cfg_matches_iface(const MsCfg *cfg, const MsIface *m)
{
    int i;
    if (!cfg || !m)
        return 0;
    if (cfg->ndpi != m->ndpi || cfg->nmap != m->nmap || cfg->active_dpi != m->active_dpi)
        return 0;
    if (cfg->cpi_default != m->cpi_default || cfg->buttons != m->buttons)
        return 0;
    if (strcmp(cfg->dpi_profile, m->dpi_profile) != 0)
        return 0;
    if (strcmp(cfg->sensor, m->sensor) != 0)
        return 0;
    for (i = 0; i < m->ndpi; i++) {
        if (cfg->dpi_steps[i] != m->dpi_steps[i])
            return 0;
    }
    for (i = 0; i < m->nmap; i++) {
        if (strcmp(cfg->map[i].physical, m->map[i].physical) != 0)
            return 0;
        if (strcmp(cfg->map[i].action, m->map[i].action) != 0)
            return 0;
    }
    return 1;
}

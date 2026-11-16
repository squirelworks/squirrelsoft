#ifndef MS_CFG_H
#define MS_CFG_H

#include "ms_iface.h"

#define MS_CFG_MAGIC "SQMS1"

typedef struct {
    char magic[8];
    char sensor[MS_NAME_LEN];
    char scroll[MS_NAME_LEN];
    char hid_report[MS_NAME_LEN];
    char dpi_profile[MS_NAME_LEN];
    int cpi_default;
    int report_hz;
    int buttons;
    int dpi_steps[MS_MAX_DPI_STEPS];
    int ndpi;
    int active_dpi;
    MsButton map[MS_MAX_BUTTONS];
    int nmap;
} MsCfg;

int ms_cfg_from_iface(const MsIface *m, MsCfg *cfg);
int ms_cfg_write(const char *path, const MsCfg *cfg);
int ms_cfg_read(const char *path, MsCfg *cfg, char *err, int errlen);
/* 1 if cfg matches iface DPI + button settings. */
int ms_cfg_matches_iface(const MsCfg *cfg, const MsIface *m);

#endif

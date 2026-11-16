#ifndef KB_CFG_H
#define KB_CFG_H

#include "kb_iface.h"

#define KB_CFG_MAGIC "SQKB1"
#define KB_CFG_MAX 4096

typedef struct {
    char magic[8];
    char layout[KB_NAME_LEN];
    int rows;
    int cols;
    int debounce_ms;
    int scan_hz;
    int nkeys;
    KbKey keys[KB_MAX_KEYS];
} KbCfg;

int kb_cfg_from_iface(const KbIface *k, KbCfg *cfg);
int kb_cfg_write(const char *path, const KbCfg *cfg);
int kb_cfg_read(const char *path, KbCfg *cfg, char *err, int errlen);
/* 1 if cfg matches iface layout/keys. */
int kb_cfg_matches_iface(const KbCfg *cfg, const KbIface *k);

#endif

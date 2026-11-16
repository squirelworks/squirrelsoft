#include "kb_cfg.h"

#include <stdio.h>
#include <string.h>

int kb_cfg_from_iface(const KbIface *k, KbCfg *cfg)
{
    if (!k || !cfg)
        return -1;
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->magic, sizeof(cfg->magic), "%s", KB_CFG_MAGIC);
    snprintf(cfg->layout, sizeof(cfg->layout), "%s", k->layout);
    cfg->rows = k->rows;
    cfg->cols = k->cols;
    cfg->debounce_ms = k->debounce_ms;
    cfg->scan_hz = k->scan_hz;
    cfg->nkeys = k->nkeys;
    memcpy(cfg->keys, k->keys, sizeof(KbKey) * (size_t)k->nkeys);
    return 0;
}

int kb_cfg_write(const char *path, const KbCfg *cfg)
{
    FILE *f;
    int i;
    if (!path || !cfg)
        return -1;
    f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "%s\n", KB_CFG_MAGIC);
    fprintf(f, "layout: %s\n", cfg->layout);
    fprintf(f, "rows: %d\n", cfg->rows);
    fprintf(f, "cols: %d\n", cfg->cols);
    fprintf(f, "debounce_ms: %d\n", cfg->debounce_ms);
    fprintf(f, "scan_hz: %d\n", cfg->scan_hz);
    for (i = 0; i < cfg->nkeys; i++)
        fprintf(f, "key %s: %d %d\n", cfg->keys[i].name, cfg->keys[i].row,
                cfg->keys[i].col);
    fclose(f);
    return 0;
}

int kb_cfg_read(const char *path, KbCfg *cfg, char *err, int errlen)
{
    KbIface tmp;
    if (kb_iface_load(path, &tmp, err, errlen) != 0)
        return -1;
    /* magic line optional — iface loader skips unknown; require SQKB1 first line */
    {
        FILE *f = fopen(path, "r");
        char line[64];
        if (!f)
            return -1;
        if (!fgets(line, sizeof(line), f) || strncmp(line, KB_CFG_MAGIC, 5) != 0) {
            fclose(f);
            if (err && errlen > 0)
                snprintf(err, errlen, "bad magic");
            return -1;
        }
        fclose(f);
    }
    return kb_cfg_from_iface(&tmp, cfg);
}

int kb_cfg_matches_iface(const KbCfg *cfg, const KbIface *k)
{
    int i;
    if (!cfg || !k)
        return 0;
    if (cfg->rows != k->rows || cfg->cols != k->cols || cfg->nkeys != k->nkeys)
        return 0;
    if (strcmp(cfg->layout, k->layout) != 0)
        return 0;
    for (i = 0; i < k->nkeys; i++) {
        if (strcmp(cfg->keys[i].name, k->keys[i].name) != 0)
            return 0;
        if (cfg->keys[i].row != k->keys[i].row || cfg->keys[i].col != k->keys[i].col)
            return 0;
    }
    return 1;
}

/*
 * HUD / HMI page model for Soft symbology tooling (Victor / ISM fighter).
 * Display symbology only — no weapons release logic or stores inventory.
 */
#ifndef HT_PAGE_H
#define HT_PAGE_H

#define HT_NAME_LEN 64
#define HT_KIND_LEN 32
#define HT_MSG_LEN 240
#define HT_MAX_ELEMENTS 64
#define HT_MAX_STATIONS 16

typedef enum {
    HT_PAGE_HUD = 0,
    HT_PAGE_MFD = 1
} HtPageType;

/* Display-only station flag for HUD stores cues (not a weapons model). */
typedef enum {
    HT_STATION_EMPTY = 0,
    HT_STATION_LOADED = 1,
    HT_STATION_SELECTED = 2,
    HT_STATION_UNSAFE = 3
} HtStationFlag;

typedef struct {
    char id[HT_NAME_LEN];
    char kind[HT_KIND_LEN]; /* attitude, airspeed, altitude, heading, aoa, gear, … */
    int x, y, w, h;
    int station; /* 0 = none; else 1..N display station index */
    HtStationFlag station_flag;
} HtElement;

typedef struct {
    HtPageType type;
    char name[HT_NAME_LEN];
    int width, height;
    HtElement el[HT_MAX_ELEMENTS];
    int nel;
} HtPage;

void ht_page_clear(HtPage *p);
int ht_page_parse(HtPage *p, const char *text, char *err, int err_cap);
int ht_page_add_element(HtPage *p, const HtElement *el, char *err, int err_cap);
void ht_page_new_hud(HtPage *p, const char *name);
void ht_page_new_mfd(HtPage *p, const char *name);
const char *ht_page_type_name(HtPageType t);
const char *ht_station_flag_name(HtStationFlag f);
int ht_station_flag_parse(const char *s, HtStationFlag *out);

#endif

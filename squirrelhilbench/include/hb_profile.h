/*
 * ISM fighter HIL bench profile (Soft UI). Hardware / harness SoT stays in
 * international-squirrel-machines. Soft loads mock or published profiles only.
 * Weapons boundary: stores display / safeing flags only — no guidance,
 * seekers, propulsion, warheads, fuzing, arming codes, or weapon protocols.
 */
#ifndef HB_PROFILE_H
#define HB_PROFILE_H

#define HB_NAME_LEN 64
#define HB_MSG_LEN 240
#define HB_MAX_CH 48

typedef enum {
    HB_CH_ACTUATOR = 0,
    HB_CH_INCEPTOR = 1,
    HB_CH_BUS = 2,
    HB_CH_STORES_DISPLAY = 3 /* display / safeing only */
} HbChannelKind;

typedef enum {
    HB_HEALTH_OK = 0,
    HB_HEALTH_DEGRADED = 1,
    HB_HEALTH_FAULT = 2,
    HB_HEALTH_OFFLINE = 3
} HbHealth;

typedef struct {
    char id[HB_NAME_LEN];
    HbChannelKind kind;
    HbHealth health;
} HbChannel;

typedef struct {
    char aircraft_id[HB_NAME_LEN];
    char transport[32]; /* mock / serial / usb — mock for CI */
    char endpoint[HB_NAME_LEN];
    int estop_latched; /* 1 = estop engaged */
    int safeing_armed; /* 0 = safe, 1 = armed display only */
    HbChannel ch[HB_MAX_CH];
    int nch;
    int connected; /* set after successful load / mock connect */
} HbProfile;

const char *hb_channel_kind_name(HbChannelKind k);
const char *hb_health_name(HbHealth h);
HbHealth hb_health_parse(const char *s);

/* Parse ISM-HIL bench profile text. Returns 0 on success. */
int hb_profile_parse(HbProfile *p, const char *text, char *err, int err_cap);

/* Mock transport connect: validates profile and marks connected. */
int hb_profile_connect_mock(HbProfile *p, char *err, int err_cap);

#endif

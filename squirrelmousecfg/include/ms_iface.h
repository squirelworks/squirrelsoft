#ifndef MS_IFACE_H
#define MS_IFACE_H

#define MS_NAME_LEN 32
#define MS_PATH_LEN 256
#define MS_MSG_LEN 128
#define MS_MAX_DPI_STEPS 16
#define MS_MAX_BUTTONS 8

typedef struct {
    char physical[MS_NAME_LEN]; /* left, right, middle, side1, side2 */
    char action[MS_NAME_LEN];   /* left, right, middle, back, forward, ... */
} MsButton;

typedef struct {
    char sensor[MS_NAME_LEN];
    int buttons; /* count from iface; may exceed mapped rows */
    char scroll[MS_NAME_LEN];
    char hid_report[MS_NAME_LEN];
    int cpi_default;
    int report_hz;
    char dpi_profile[MS_NAME_LEN];
    int dpi_steps[MS_MAX_DPI_STEPS];
    int ndpi;
    int active_dpi; /* currently selected CPI; defaults to cpi_default */
    MsButton map[MS_MAX_BUTTONS];
    int nmap;
    char path[MS_PATH_LEN];
} MsIface;

/* Load ISM-HID style mouse interface text. Returns 0 on success. */
int ms_iface_load(const char *path, MsIface *out, char *err, int errlen);

#endif

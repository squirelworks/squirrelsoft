/*
 * ISM device interface files for Soft bench tests (Eric / consumer SoT).
 * Soft does not design keyboards or mice — it loads published interfaces.
 */
#ifndef IB_IFACE_H
#define IB_IFACE_H

#define IB_NAME_LEN 64
#define IB_MSG_LEN 240
#define IB_MAX_KEYS 128
#define IB_MAX_ROWS 8
#define IB_MAX_COLS 20

typedef struct {
    char layout[IB_NAME_LEN]; /* e.g. 60-percent */
    int rows, cols;
    char diode[32]; /* col2row or row2col */
    char hid_report[32]; /* boot_keyboard */
    int debounce_ms;
    int scan_hz;
    /* sparse key map: name -> row,col */
    char key_name[IB_MAX_KEYS][16];
    int key_row[IB_MAX_KEYS];
    int key_col[IB_MAX_KEYS];
    int nkeys;
} IbKeyboardIface;

typedef struct {
    char sensor[32]; /* optical_stub / ball_stub */
    int buttons;
    char scroll[32]; /* vertical / vertical_tilt */
    char hid_report[32]; /* boot_mouse */
    int cpi_default;
    int report_hz;
} IbMouseIface;

int ib_keyboard_parse(IbKeyboardIface *k, const char *text, char *err, int err_cap);
int ib_mouse_parse(IbMouseIface *m, const char *text, char *err, int err_cap);

#endif

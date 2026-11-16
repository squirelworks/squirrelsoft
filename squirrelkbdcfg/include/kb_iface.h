#ifndef KB_IFACE_H
#define KB_IFACE_H

#define KB_MAX_ROWS 8
#define KB_MAX_COLS 20
#define KB_MAX_KEYS 128
#define KB_NAME_LEN 16
#define KB_PATH_LEN 256
#define KB_MSG_LEN 128

typedef struct {
    char name[KB_NAME_LEN];
    int row;
    int col;
} KbKey;

typedef struct {
    char layout[KB_NAME_LEN];
    int rows;
    int cols;
    int debounce_ms;
    int scan_hz;
    int nkeys;
    KbKey keys[KB_MAX_KEYS];
    char path[KB_PATH_LEN];
} KbIface;

/* Load ISM-HID style keyboard interface text. Returns 0 on success. */
int kb_iface_load(const char *path, KbIface *out, char *err, int errlen);

#endif

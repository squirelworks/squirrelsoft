#ifndef RM_IFACE_H
#define RM_IFACE_H

#define RM_NAME_LEN 32
#define RM_PATH_LEN 256
#define RM_MSG_LEN 160
#define RM_SPD_MAX 256
#define RM_BANK_MAX 65536

typedef struct {
    char part[RM_NAME_LEN];
    int density_kib;
    int banks;
    int spd_i2c_addr; /* typically 0x50 */
    int spd_len;
    unsigned char spd[RM_SPD_MAX];
    int dq_width;
    int pattern_bytes; /* synthetic bank size for CI */
    unsigned int lcg_seed;
    char path[RM_PATH_LEN];
} RmIface;

/* Load ISM-MEM memory interface text. Returns 0 on success. */
int rm_iface_load(const char *path, RmIface *out, char *err, int errlen);
int rm_iface_parse(RmIface *out, const char *text, char *err, int errlen);

#endif

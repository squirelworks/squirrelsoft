#ifndef SB_BOARD_H
#define SB_BOARD_H

#include "sb_hydro.h"

#define SB_MAX_CH 16

typedef struct {
    char board_id[64];
    int nch;
    char ch_id[SB_MAX_CH][32];
    char ch_role[SB_MAX_CH][48];
    int no_weapons;
    int scale_model_only;
} SbBoardIface;

typedef struct {
    int ok;
    int npass;
    int nfail;
    char summary[SB_MSG_LEN];
} SbBoardResult;

int sb_board_parse(SbBoardIface *b, const char *text, char *err, int err_cap);
int sb_board_selftest(const SbBoardIface *b, SbBoardResult *res);

#endif

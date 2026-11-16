#ifndef MS_BTN_H
#define MS_BTN_H

#include "ms_iface.h"

/* Print physical → action button map. */
int ms_btn_print(const MsIface *m, char *out, int outlen);

/* Remap one physical button to an action. Adds row if missing. */
int ms_btn_remap(MsIface *m, const char *physical, const char *action);

/* Look up action for physical name; NULL if unset. */
const char *ms_btn_action(const MsIface *m, const char *physical);

#endif

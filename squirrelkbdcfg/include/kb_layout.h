#ifndef KB_LAYOUT_H
#define KB_LAYOUT_H

#include "kb_iface.h"

/* Print ASCII matrix of key names (empty cells as "."). */
int kb_layout_print(const KbIface *k, char *out, int outlen);

/* Look up key name at row/col; returns NULL if empty. */
const char *kb_layout_at(const KbIface *k, int row, int col);

#endif

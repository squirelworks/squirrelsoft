#include "kb_layout.h"

#include <stdio.h>
#include <string.h>

const char *kb_layout_at(const KbIface *k, int row, int col)
{
    int i;
    if (!k)
        return NULL;
    for (i = 0; i < k->nkeys; i++) {
        if (k->keys[i].row == row && k->keys[i].col == col)
            return k->keys[i].name;
    }
    return NULL;
}

int kb_layout_print(const KbIface *k, char *out, int outlen)
{
    int r, c, n = 0;
    if (!k || !out || outlen < 8)
        return -1;
    out[0] = 0;
    n += snprintf(out + n, outlen - n, "layout %s %dx%d (%d keys)\n",
                  k->layout, k->rows, k->cols, k->nkeys);
    for (r = 0; r < k->rows; r++) {
        for (c = 0; c < k->cols; c++) {
            const char *nm = kb_layout_at(k, r, c);
            if (n >= outlen - 8)
                return -1;
            if (nm)
                n += snprintf(out + n, outlen - n, "%-4s", nm);
            else
                n += snprintf(out + n, outlen - n, "%-4s", ".");
        }
        n += snprintf(out + n, outlen - n, "\n");
    }
    return 0;
}

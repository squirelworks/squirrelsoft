/* dbggui: the SquirrelDebug window on libs/sqgui. Along the top: Continue,
 * Stop, Step, Next, Run to, the state, and the code's mode (Auto, 16,
 * 32). Below: the code around the pc (a click in the left margin sets or
 * clears a breakpoint, a click on a line picks it for Run to, the wheel
 * scrolls), the registers (those the last step or run changed in red),
 * memory (an address typed above it; the wheel scrolls), the breakpoints,
 * what the target printed and what commands answered, and a command line
 * that takes dbg_command's commands. Ctrl+S steps, Ctrl+N goes over,
 * Ctrl+R continues, Ctrl+B sets or clears a breakpoint on the line
 * picked. Original code.
 */
#ifndef DBGGUI_H
#define DBGGUI_H

#include "dbg.h"
#include "sqgui.h"

#define DBG_GUI_W 960
#define DBG_GUI_H 680
#define DBG_GUI_ROWS 26
#define DBG_GUI_MEMROWS 7

enum { DG_CONTINUE, DG_STOP, DG_STEP, DG_NEXT, DG_RUNTO, DG_NBUTTONS };

typedef struct {
    Dbg *dbg;
    char title[96];            /* what the target is, for the window's title */
    char cmd[200];
    char log[8192];            /* commands, their answers and the target's output */
    size_t nlog;
    unsigned long out_seen;    /* of dbg->out_total */
    unsigned gen_seen;
    int state_seen;
    uint32_t top;              /* the first line of code shown */
    int follow;                /* keep the pc in view */
    uint32_t sel;              /* the line picked */
    int have_sel;
    uint32_t row_addr[DBG_GUI_ROWS];
    char row_text[DBG_GUI_ROWS][112];
    int nrows;
    char mem_text[32];
    uint32_t mem_at;
    uint8_t mem[16 * DBG_GUI_MEMROWS];
    int nmem;
    int mode_sel;              /* 0 auto, 1 16-bit, 2 32-bit */
    int stale;                 /* the views need reading again */
} DbgGui;

void dbg_gui_init(DbgGui *g, Dbg *d);
void dbg_gui_frame(DbgGui *g, sqg_ui *ui);
/* Add a line to the log */
void dbg_gui_log(DbgGui *g, const char *s);
/* Where things are, for tests */
sqg_rect dbg_gui_button(const sqg_ui *ui, int which);
sqg_rect dbg_gui_row(int i);       /* a line of code; its first 14 pixels are the margin */
sqg_rect dbg_gui_cmd_rect(void);
sqg_rect dbg_gui_mem_rect(void);   /* the memory address field */
/* The window (dbg_sdl.c, or dbg_nosdl.c when there is no SDL2) */
int dbg_gui_run(DbgGui *g, int max_frames);

#endif

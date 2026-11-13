/* dbggui: the SquirrelDebug window. Original code. */
#include "dbggui.h"

#include <string.h>

#define RED SQG_RGB(0xC0, 0x00, 0x00)
#define MARGIN 14
#define ROW_H 12

static const sqg_rect P_CODE = {8, 36, 600, 8 + DBG_GUI_ROWS * ROW_H};
static const sqg_rect P_REGS = {616, 36, 336, 8 + DBG_GUI_ROWS * ROW_H};
static const sqg_rect P_MEM = {8, 386, 600, 8 + DBG_GUI_MEMROWS * ROW_H};
static const sqg_rect P_BPS = {616, 386, 336, 8 + DBG_GUI_MEMROWS * ROW_H};
static const sqg_rect P_LOG = {8, 486, 944, 158};
static const sqg_rect MEM_FIELD = {64, 362, 150, 19};
static const sqg_rect CMD_FIELD = {8, 652, 944, 22};
static const char *const BUTTONS[DG_NBUTTONS] = {"Continue", "Stop", "Step", "Next", "Run to"};

sqg_rect dbg_gui_row(int i) { return sqg_rect_make(P_CODE.x + 4, P_CODE.y + 4 + i * ROW_H, P_CODE.w - 8, ROW_H); }
sqg_rect dbg_gui_cmd_rect(void) { return CMD_FIELD; }
sqg_rect dbg_gui_mem_rect(void) { return MEM_FIELD; }

sqg_rect dbg_gui_button(const sqg_ui *ui, int which) {
    sqg_row row = sqg_row_begin(sqg_rect_make(8, 6, 600, sqg_line_h(ui)), 4);
    sqg_rect r = {0, 0, 0, 0};
    for (int i = 0; i <= which && i < DG_NBUTTONS; i++) r = sqg_row_take(&row, sqg_button_w(ui, BUTTONS[i]));
    return r;
}

void dbg_gui_log(DbgGui *g, const char *s) {
    size_t n = strlen(s);
    if (n > sizeof g->log / 2) {
        s += n - sizeof g->log / 2;
        n = sizeof g->log / 2;
    }
    if (g->nlog + n + 2 > sizeof g->log) { /* drop the oldest lines */
        size_t drop = g->nlog + n + 2 - sizeof g->log;
        while (drop < g->nlog && g->log[drop - 1] != '\n') drop++;
        memmove(g->log, g->log + drop, g->nlog - drop);
        g->nlog -= drop;
    }
    memcpy(g->log + g->nlog, s, n);
    g->nlog += n;
    if (!n || s[n - 1] != '\n') g->log[g->nlog++] = '\n';
    g->log[g->nlog] = 0;
}

void dbg_gui_init(DbgGui *g, Dbg *d) {
    memset(g, 0, sizeof *g);
    g->dbg = d;
    g->follow = 1;
    g->stale = 1;
    g->state_seen = -1;
    g->mode_sel = d->bits_forced == 16 ? 1 : d->bits_forced == 32 ? 2 : 0;
    g->mem_at = 0;
    memcpy(g->mem_text, "0", 2);
}

/* A start some instructions before addr whose decoding lands on addr */
static uint32_t back(DbgGui *g, uint32_t addr, int k) {
    Dbg *d = g->dbg;
    uint8_t b[64 + X86_MAXLEN];
    uint32_t span = addr - d->cs_base < 64 ? addr - d->cs_base : 64;
    if (!span || dbg_read(d, addr - span, b, span + X86_MAXLEN) < (long)span) return addr;
    for (uint32_t off = span; off > 0; off--) {
        uint32_t starts[64];
        int n = 0;
        uint32_t pos = span - off;
        while (pos < span && n < 64) {
            X86Insn in;
            starts[n++] = pos;
            x86_disasm(b + pos, sizeof b - pos, 0, 0, d->bits, &in);
            pos += (uint32_t)in.len;
        }
        if (pos == span) return addr - span + starts[n > k ? n - k : 0];
    }
    return addr;
}

static void reload(DbgGui *g) {
    Dbg *d = g->dbg;
    static uint8_t code[DBG_GUI_ROWS * X86_MAXLEN];
    if (d->state != DBG_STOPPED) return; /* the views keep what they last showed */
    uint32_t pc = dbg_pc(d);
    if (g->follow) { /* the pc on one of the lines from the top's second to the fourth from the bottom, or start again around it */
        int seen = 0;
        for (int i = 1; i < g->nrows - 3; i++) seen |= g->row_addr[i] == pc;
        if (!seen) g->top = back(g, pc, 3);
    }
    g->nrows = 0;
    long n = dbg_read(d, g->top, code, sizeof code);
    size_t pos = 0;
    while (n > 0 && g->nrows < DBG_GUI_ROWS && pos < (size_t)n) {
        int len = dbg_disasm_at(d, g->top + (uint32_t)pos, code + pos, (size_t)n - pos, g->row_text[g->nrows],
                                sizeof g->row_text[0], NULL);
        g->row_addr[g->nrows++] = g->top + (uint32_t)pos;
        pos += (size_t)len;
    }
    long m = dbg_read(d, g->mem_at, g->mem, sizeof g->mem);
    g->nmem = m > 0 ? (int)m : 0;
}

static void say_error(DbgGui *g) {
    char b[160] = "error: ";
    size_t k = strlen(b);
    size_t n = strlen(g->dbg->err);
    if (n > sizeof b - k - 1) n = sizeof b - k - 1;
    memcpy(b + k, g->dbg->err, n);
    b[k + n] = 0;
    dbg_gui_log(g, b);
}

static void act(DbgGui *g, int which) {
    Dbg *d = g->dbg;
    int rc = 0;
    switch (which) {
    case DG_CONTINUE: rc = dbg_continue(d); break;
    case DG_STOP: rc = d->state == DBG_RUNNING ? dbg_stop(d) : 0; break;
    case DG_STEP: rc = dbg_step(d); break;
    case DG_NEXT: rc = dbg_next(d); break;
    case DG_RUNTO:
        if (!g->have_sel) {
            dbg_gui_log(g, "Run to: pick a line of code first");
            return;
        }
        rc = dbg_until(d, g->sel);
        break;
    }
    if (rc) say_error(g);
    g->follow = 1;
    g->stale = 1;
}

static void toggle_break(DbgGui *g, uint32_t a) {
    Dbg *d = g->dbg;
    if (dbg_is_break(d, a) ? dbg_unbreak(d, a) : dbg_break(d, a)) say_error(g);
    g->stale = 1;
}

static void hex(char *o, uint32_t v, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        o[i] = "0123456789abcdef"[v & 15];
        v >>= 4;
    }
}

static void panel(sqg_ui *ui, sqg_rect r) {
    sqg_bevel(ui->cv, r, 1, 0, ui->theme);
    sqg_fill(ui->cv, sqg_rect_inset(r, 2), ui->theme->window);
}

static void draw_code(DbgGui *g, sqg_ui *ui) {
    Dbg *d = g->dbg;
    const sqg_theme *t = ui->theme;
    panel(ui, P_CODE);
    sqg_rect old = sqg_clip(ui->cv, sqg_rect_inset(P_CODE, 2));
    uint32_t pc = d->state == DBG_STOPPED ? dbg_pc(d) : 0xFFFFFFFFu;
    for (int i = 0; i < g->nrows; i++) {
        sqg_rect r = dbg_gui_row(i);
        uint32_t a = g->row_addr[i];
        uint32_t fg = t->window_text;
        if (a == pc) {
            sqg_fill(ui->cv, sqg_rect_make(r.x + MARGIN, r.y, r.w - MARGIN, r.h), t->select);
            fg = t->select_text;
        }
        if (g->have_sel && a == g->sel) sqg_frame(ui->cv, sqg_rect_make(r.x + MARGIN, r.y, r.w - MARGIN, r.h), t->accent);
        if (dbg_is_break(d, a)) sqg_fill(ui->cv, sqg_rect_make(r.x + 3, r.y + 2, 8, 8), RED);
        /* the text without its two marker columns: the margin shows them */
        sqg_text(ui->cv, r.x + MARGIN + 2, r.y + 2, g->row_text[i] + 3, fg, 1);
    }
    if (!g->nrows)
        sqg_text(ui->cv, P_CODE.x + 8, P_CODE.y + 8,
                 d->state == DBG_RUNNING ? "Running. Stop to look." : d->state == DBG_OFF ? "Not connected." : "No code to show.",
                 t->text_disabled, 1);
    ui->cv->clip = old;
}

static void draw_regs(DbgGui *g, sqg_ui *ui) {
    static const int ORDER[16] = {DBG_EAX, DBG_EBX, DBG_ECX, DBG_EDX, DBG_ESI, DBG_EDI, DBG_EBP, DBG_ESP,
                                  DBG_EIP, DBG_EFLAGS, DBG_CS, DBG_SS, DBG_DS, DBG_ES, DBG_FS, DBG_GS};
    static const char *const FL[9] = {"CF", "PF", "AF", "ZF", "SF", "TF", "IF", "DF", "OF"};
    static const int FB[9] = {0, 2, 4, 6, 7, 8, 9, 10, 11};
    Dbg *d = g->dbg;
    const sqg_theme *t = ui->theme;
    panel(ui, P_REGS);
    if (d->state == DBG_OFF) return;
    int x = P_REGS.x + 8, y = P_REGS.y + 8;
    for (int i = 0; i < 16; i++) {
        int r = ORDER[i];
        char line[24];
        size_t k = strlen(DBG_REG_NAMES[r]);
        memcpy(line, DBG_REG_NAMES[r], k);
        while (k < 7) line[k++] = ' ';
        int digits = r >= DBG_CS ? 4 : 8;
        hex(line + k, d->reg[r], digits);
        line[k + (size_t)digits] = 0;
        uint32_t fg = d->reg[r] != d->prev[r] && d->state == DBG_STOPPED ? RED : t->window_text;
        /* eax to eflags down the left; the segment registers in two columns of three below */
        int xx = i < 10 ? x : x + (i - 10) / 3 * 96, yy = i < 10 ? y + i * ROW_H : y + (11 + (i - 10) % 3) * ROW_H;
        sqg_text(ui->cv, xx, yy, line, fg, 1);
    }
    /* the flags beside eflags */
    char fl[40];
    size_t k = 0;
    for (int i = 0; i < 9; i++)
        if (d->reg[DBG_EFLAGS] >> FB[i] & 1) {
            if (k) fl[k++] = ' ';
            memcpy(fl + k, FL[i], 2);
            k += 2;
        }
    fl[k] = 0;
    sqg_text(ui->cv, x + 120, y + 9 * ROW_H, fl, t->window_text, 1);
    int yy = y + 15 * ROW_H;
    if (d->cr_known) {
        char cr[40] = "cr0    ";
        hex(cr + 7, d->cr0, 8);
        memcpy(cr + 15, "  cr3 ", 6);
        hex(cr + 21, d->cr3, 8);
        cr[29] = 0;
        sqg_text(ui->cv, x, yy, cr, t->window_text, 1);
        yy += ROW_H;
    }
    sqg_text(ui->cv, x, yy, d->bits == 16 ? "16-bit code" : "32-bit code", t->window_text, 1);
    yy += ROW_H;
    if (d->state == DBG_STOPPED) {
        char where[160];
        uint32_t off;
        const char *name = dbg_sym_at(d, dbg_pc(d), &off);
        if (name) {
            size_t n = strlen(name);
            if (n > 40) n = 40;
            memcpy(where, "in ", 3);
            memcpy(where + 3, name, n);
            where[3 + n] = 0;
            sqg_text(ui->cv, x, yy, where, t->window_text, 1);
        }
    }
}

static void draw_mem(DbgGui *g, sqg_ui *ui) {
    const sqg_theme *t = ui->theme;
    panel(ui, P_MEM);
    for (int row = 0; row * 16 < g->nmem; row++) {
        char line[96];
        size_t k = 0;
        uint32_t a = g->mem_at + (uint32_t)row * 16;
        hex(line, a, 8);
        k = 8;
        line[k++] = ' ';
        line[k++] = ' ';
        for (int i = 0; i < 16; i++) {
            int at = row * 16 + i;
            if (at < g->nmem) hex(line + k, g->mem[at], 2);
            else line[k] = line[k + 1] = ' ';
            k += 2;
            line[k++] = ' ';
            if (i == 7) line[k++] = ' ';
        }
        line[k++] = ' ';
        for (int i = 0; i < 16 && row * 16 + i < g->nmem; i++) {
            uint8_t c = g->mem[row * 16 + i];
            line[k++] = c >= 0x20 && c < 0x7F ? (char)c : '.';
        }
        line[k] = 0;
        sqg_text(ui->cv, P_MEM.x + 8, P_MEM.y + 6 + row * ROW_H, line, t->window_text, 1);
    }
}

static void draw_bps(DbgGui *g, sqg_ui *ui) {
    Dbg *d = g->dbg;
    panel(ui, P_BPS);
    for (int i = 0; i < d->nbp && i < DBG_GUI_MEMROWS; i++) {
        char a[96];
        dbg_addr_text(d, d->bp[i], a, sizeof a);
        sqg_text(ui->cv, P_BPS.x + 8, P_BPS.y + 6 + i * ROW_H, a, ui->theme->window_text, 1);
    }
    if (!d->nbp) sqg_text(ui->cv, P_BPS.x + 8, P_BPS.y + 6, "None: click the margin of a line.", ui->theme->text_disabled, 1);
}

static void draw_log(DbgGui *g, sqg_ui *ui) {
    panel(ui, P_LOG);
    int lines = (P_LOG.h - 8) / ROW_H;
    size_t start = g->nlog;
    int n = 0;
    if (start && g->log[start - 1] == '\n') start--;
    while (start > 0 && n < lines) {
        start--;
        if (g->log[start] == '\n' && ++n == lines) {
            start++;
            break;
        }
    }
    sqg_rect old = sqg_clip(ui->cv, sqg_rect_inset(P_LOG, 2));
    int y = P_LOG.y + 5;
    char line[200];
    for (size_t i = start; i < g->nlog;) {
        size_t e = i;
        while (e < g->nlog && g->log[e] != '\n') e++;
        size_t k = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, g->log + i, k);
        line[k] = 0;
        sqg_text(ui->cv, P_LOG.x + 6, y, line, ui->theme->window_text, 1);
        y += ROW_H;
        i = e + 1;
    }
    ui->cv->clip = old;
}

static void take_output(DbgGui *g) {
    Dbg *d = g->dbg;
    if (d->out_total == g->out_seen) return;
    unsigned long fresh = d->out_total - g->out_seen;
    size_t n = fresh < d->nout ? (size_t)fresh : d->nout;
    static char b[DBG_OUT + 1];
    memcpy(b, d->out + d->nout - n, n);
    b[n] = 0;
    dbg_gui_log(g, b);
    g->out_seen = d->out_total;
}

void dbg_gui_frame(DbgGui *g, sqg_ui *ui) {
    Dbg *d = g->dbg;
    const sqg_theme *t = ui->theme;
    sqg_fill(ui->cv, sqg_rect_make(0, 0, ui->cv->w, ui->cv->h), t->face);
    if (d->state == DBG_RUNNING) dbg_poll(d, 0);
    take_output(g);
    if (d->state != g->state_seen) {
        if (g->state_seen == DBG_RUNNING && d->state == DBG_STOPPED) g->follow = 1;
        if (g->state_seen >= 0 && d->state != DBG_RUNNING) {
            char s[200];
            dbg_status_text(d, s, sizeof s);
            dbg_gui_log(g, s);
        }
        g->stale = 1;
    }
    if (g->stale || d->gen != g->gen_seen) {
        reload(g);
        g->stale = 0;
        g->gen_seen = d->gen;
        g->state_seen = d->state;
    }

    /* the toolbar */
    for (int i = 0; i < DG_NBUTTONS; i++)
        if (sqg_button(ui, sqg_id_str(BUTTONS[i]), dbg_gui_button(ui, i), BUTTONS[i])) act(g, i);
    if (ui->in.ctrl_key == 's') act(g, DG_STEP);
    if (ui->in.ctrl_key == 'n') act(g, DG_NEXT);
    if (ui->in.ctrl_key == 'r') act(g, DG_CONTINUE);
    if (ui->in.ctrl_key == 'b' && g->have_sel) toggle_break(g, g->sel);
    sqg_rect last = dbg_gui_button(ui, DG_NBUTTONS - 1);
    char status[200];
    dbg_status_text(d, status, sizeof status);
    sqg_rect sr = sqg_rect_make(last.x + last.w + 12, 6, 952 - 160 - (last.x + last.w + 12), sqg_line_h(ui));
    sqg_rect old = sqg_clip(ui->cv, sr);
    sqg_text(ui->cv, sr.x, sr.y + 4, status, d->state == DBG_RUNNING ? RED : t->text, 1);
    ui->cv->clip = old;
    static const char *const MODES[3] = {"Auto", "16", "32"};
    if (sqg_segments(ui, sqg_id_str("mode"), sqg_rect_make(952 - 150, 6, 150, sqg_line_h(ui)), MODES, 3, &g->mode_sel)) {
        d->bits_forced = g->mode_sel == 1 ? 16 : g->mode_sel == 2 ? 32 : 0;
        if (d->state == DBG_STOPPED && dbg_refresh(d)) say_error(g);
        g->stale = 1;
    }

    /* the code: clicks and the wheel */
    if (sqg_rect_contains(P_CODE, ui->in.mx, ui->in.my)) {
        if (ui->in.pressed)
            for (int i = 0; i < g->nrows; i++) {
                sqg_rect r = dbg_gui_row(i);
                if (!sqg_rect_contains(r, ui->in.mx, ui->in.my)) continue;
                if (ui->in.mx < r.x + MARGIN) toggle_break(g, g->row_addr[i]);
                else {
                    g->sel = g->row_addr[i];
                    g->have_sel = 1;
                }
            }
        if (ui->in.wheel && d->state == DBG_STOPPED && g->nrows) {
            g->follow = 0;
            if (ui->in.wheel < 0) g->top = g->row_addr[g->nrows > 3 ? 3 : g->nrows - 1];
            else g->top = back(g, g->top, 3);
            g->stale = 1;
        }
    }
    if (sqg_rect_contains(P_MEM, ui->in.mx, ui->in.my) && ui->in.wheel) {
        g->mem_at -= (uint32_t)(ui->in.wheel * 16 * 2);
        g->stale = 1;
    }
    if (g->stale) { /* what was clicked shows this frame */
        reload(g);
        g->stale = 0;
        g->gen_seen = d->gen;
        g->state_seen = d->state;
    }

    draw_code(g, ui);
    draw_regs(g, ui);
    sqg_text(ui->cv, 8, MEM_FIELD.y + 5, "Memory", t->text, 1);
    if (sqg_textfield(ui, sqg_id_str("memaddr"), MEM_FIELD, g->mem_text, sizeof g->mem_text)) {
        uint32_t a;
        if (dbg_eval(d, g->mem_text, &a)) dbg_gui_log(g, "Memory: not an address");
        else {
            g->mem_at = a;
            reload(g);
        }
    }
    draw_mem(g, ui);
    sqg_text(ui->cv, P_BPS.x, MEM_FIELD.y + 5, "Breakpoints", t->text, 1);
    if (d->nbp && sqg_button(ui, sqg_id_str("clearbps"), sqg_rect_make(P_BPS.x + P_BPS.w - 80, MEM_FIELD.y, 80, MEM_FIELD.h), "Clear all")) {
        while (d->nbp)
            if (dbg_unbreak(d, d->bp[d->nbp - 1])) {
                say_error(g);
                break;
            }
        g->stale = 1;
    }
    draw_bps(g, ui);
    draw_log(g, ui);
    if (sqg_textfield(ui, sqg_id_str("cmd"), CMD_FIELD, g->cmd, sizeof g->cmd) && g->cmd[0]) {
        static char out[4096];
        char echo[220] = "> ";
        size_t n = strlen(g->cmd);
        memcpy(echo + 2, g->cmd, n + 1);
        dbg_gui_log(g, echo);
        dbg_command(d, g->cmd, out, sizeof out);
        if (out[0]) dbg_gui_log(g, out);
        g->cmd[0] = 0;
        ui->cursor = 0;
        g->follow = 1;
        g->stale = 1;
        take_output(g);
    }
}

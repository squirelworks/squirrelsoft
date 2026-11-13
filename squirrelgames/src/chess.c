/* chess: the rules, notation and a computer player (see chess.h). Original code. */
#include "chess.h"

#include <string.h>

static int file_of(int s) { return s & 7; }
static int rank_of(int s) { return s >> 3; }
static int on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
static int abs_i(int v) { return v < 0 ? -v : v; }
static int same_text(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}
static int side_of(int piece) { return piece > 0 ? CHESS_WHITE : CHESS_BLACK; }

static const int KNIGHT[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int KING[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
/* the first four straight, the last four diagonal */
static const int RAYS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

/* ---- the start, and FEN ---- */

static const char START[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static int piece_of_letter(char ch) {
    static const char L[] = "pnbrqk";
    for (int i = 0; i < 6; i++) {
        if (ch == L[i]) return -(i + 1);
        if (ch == L[i] - 32) return i + 1;
    }
    return 0;
}

static char letter_of(int piece) {
    static const char L[] = ".PNBRQK";
    char ch = L[abs_i(piece)];
    return piece < 0 ? (char)(ch + 32) : ch;
}

static int king_square(const ChessPos *p, int side) {
    int8_t k = (int8_t)(side == CHESS_WHITE ? CHESS_KING : -CHESS_KING);
    for (int s = 0; s < 64; s++)
        if (p->sq[s] == k) return s;
    return -1;
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *read_count(const char *s, int *v) {
    s = skip_spaces(s);
    if (*s < '0' || *s > '9') return NULL;
    int x = 0;
    while (*s >= '0' && *s <= '9' && x < 100000) x = x * 10 + (*s++ - '0');
    *v = x;
    return s;
}

static int parse_fen(ChessPos *p, const char *f) {
    memset(p, 0, sizeof *p);
    int rank = 7, file = 0;
    f = skip_spaces(f);
    for (; *f && *f != ' '; f++) {
        if (*f == '/') {
            if (file != 8 || rank == 0) return -1;
            rank--, file = 0;
        } else if (*f >= '1' && *f <= '8') {
            file += *f - '0';
            if (file > 8) return -1;
        } else {
            int pc = piece_of_letter(*f);
            if (!pc || file > 7) return -1;
            p->sq[CHESS_SQ(file, rank)] = (int8_t)pc;
            file++;
        }
    }
    if (rank != 0 || file != 8) return -1;
    f = skip_spaces(f);
    if (*f == 'w') p->side = CHESS_WHITE;
    else if (*f == 'b') p->side = CHESS_BLACK;
    else return -1;
    f = skip_spaces(f + 1);
    p->castle = 0;
    if (*f == '-') f++;
    else
        for (; *f && *f != ' '; f++) {
            if (*f == 'K') p->castle |= CHESS_WK;
            else if (*f == 'Q') p->castle |= CHESS_WQ;
            else if (*f == 'k') p->castle |= CHESS_BK;
            else if (*f == 'q') p->castle |= CHESS_BQ;
            else return -1;
        }
    f = skip_spaces(f);
    p->ep = -1;
    if (*f == '-') f++;
    else if (*f >= 'a' && *f <= 'h' && (f[1] == '3' || f[1] == '6')) {
        p->ep = CHESS_SQ(*f - 'a', f[1] - '1');
        f += 2;
    } else return -1;
    p->halfmove = 0, p->fullmove = 1;
    const char *g = read_count(f, &p->halfmove);
    if (g) {
        f = g;
        g = read_count(f, &p->fullmove);
        if (!g) return -1;
        f = g;
        if (p->fullmove < 1) p->fullmove = 1;
    }
    if (*skip_spaces(f)) return -1;
    /* one king each; no pawn on the first or last rank; castling only with the pieces home */
    int kings[2] = {0, 0};
    for (int s = 0; s < 64; s++) {
        if (abs_i(p->sq[s]) == CHESS_KING) kings[side_of(p->sq[s])]++;
        if (abs_i(p->sq[s]) == CHESS_PAWN && (rank_of(s) == 0 || rank_of(s) == 7)) return -1;
    }
    if (kings[0] != 1 || kings[1] != 1) return -1;
    if (p->sq[4] != CHESS_KING) p->castle &= ~(CHESS_WK | CHESS_WQ);
    if (p->sq[60] != -CHESS_KING) p->castle &= ~(CHESS_BK | CHESS_BQ);
    if (p->sq[7] != CHESS_ROOK) p->castle &= ~CHESS_WK;
    if (p->sq[0] != CHESS_ROOK) p->castle &= ~CHESS_WQ;
    if (p->sq[63] != -CHESS_ROOK) p->castle &= ~CHESS_BK;
    if (p->sq[56] != -CHESS_ROOK) p->castle &= ~CHESS_BQ;
    if (chess_attacked(p, king_square(p, !p->side), p->side)) return -1; /* the side not to move in check */
    return 0;
}

static void start_history(Chess *c) { c->n = 0; }

void chess_new(Chess *c, uint32_t seed) {
    memset(c, 0, sizeof *c);
    parse_fen(&c->pos, START);
    c->human[CHESS_WHITE] = 1;
    c->level = 3;
    c->seed = seed;
}

int chess_set_fen(Chess *c, const char *fen) {
    ChessPos p;
    if (parse_fen(&p, fen)) return -1;
    c->pos = p;
    start_history(c);
    return 0;
}

size_t chess_fen(const ChessPos *p, char *out, size_t cap) {
    char b[100];
    size_t n = 0;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int pc = p->sq[CHESS_SQ(f, r)];
            if (!pc) {
                empty++;
                continue;
            }
            if (empty) b[n++] = (char)('0' + empty), empty = 0;
            b[n++] = letter_of(pc);
        }
        if (empty) b[n++] = (char)('0' + empty);
        if (r) b[n++] = '/';
    }
    b[n++] = ' ';
    b[n++] = p->side == CHESS_WHITE ? 'w' : 'b';
    b[n++] = ' ';
    if (!p->castle) b[n++] = '-';
    if (p->castle & CHESS_WK) b[n++] = 'K';
    if (p->castle & CHESS_WQ) b[n++] = 'Q';
    if (p->castle & CHESS_BK) b[n++] = 'k';
    if (p->castle & CHESS_BQ) b[n++] = 'q';
    b[n++] = ' ';
    if (p->ep < 0) b[n++] = '-';
    else b[n++] = (char)('a' + file_of(p->ep)), b[n++] = (char)('1' + rank_of(p->ep));
    int counts[2] = {p->halfmove, p->fullmove};
    for (int k = 0; k < 2; k++) {
        b[n++] = ' ';
        char d[12];
        int nd = 0, v = counts[k] < 0 ? 0 : counts[k];
        do d[nd++] = (char)('0' + v % 10);
        while ((v /= 10) && nd < 11);
        while (nd) b[n++] = d[--nd];
    }
    for (size_t i = 0; i < n && i + 1 < cap; i++) out[i] = b[i];
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return n;
}

/* ---- attacks and moves ---- */

int chess_attacked(const ChessPos *p, int sq, int by) {
    if (sq < 0) return 0;
    int f = file_of(sq), r = rank_of(sq), sign = by == CHESS_WHITE ? 1 : -1;
    int pr = r - sign; /* where an attacking pawn stands */
    for (int df = -1; df <= 1; df += 2)
        if (on_board(f + df, pr) && p->sq[CHESS_SQ(f + df, pr)] == sign * CHESS_PAWN) return 1;
    for (int k = 0; k < 8; k++) {
        int nf = f + KNIGHT[k][0], nr = r + KNIGHT[k][1];
        if (on_board(nf, nr) && p->sq[CHESS_SQ(nf, nr)] == sign * CHESS_KNIGHT) return 1;
        nf = f + KING[k][0], nr = r + KING[k][1];
        if (on_board(nf, nr) && p->sq[CHESS_SQ(nf, nr)] == sign * CHESS_KING) return 1;
    }
    for (int d = 0; d < 8; d++) {
        int nf = f + RAYS[d][0], nr = r + RAYS[d][1];
        while (on_board(nf, nr)) {
            int pc = p->sq[CHESS_SQ(nf, nr)];
            if (pc) {
                int t = pc * sign;
                if (t == CHESS_QUEEN || t == (d < 4 ? CHESS_ROOK : CHESS_BISHOP)) return 1;
                break;
            }
            nf += RAYS[d][0], nr += RAYS[d][1];
        }
    }
    return 0;
}

int chess_in_check(const ChessPos *p) { return chess_attacked(p, king_square(p, p->side), !p->side); }

static void play(ChessPos *p, int m) {
    int from = CHESS_FROM(m), to = CHESS_TO(m), promo = CHESS_PROMO(m);
    int pc = p->sq[from], kind = abs_i(pc), sign = pc > 0 ? 1 : -1;
    int captured = p->sq[to];
    p->halfmove = kind == CHESS_PAWN || captured ? 0 : p->halfmove + 1;
    if (kind == CHESS_PAWN && to == p->ep) p->sq[to - 8 * sign] = 0; /* en passant */
    p->sq[to] = (int8_t)(promo ? sign * promo : pc);
    p->sq[from] = 0;
    if (kind == CHESS_KING && abs_i(to - from) == 2) { /* castling: the rook too */
        int rf = to > from ? from + 3 : from - 4, rt = to > from ? from + 1 : from - 1;
        p->sq[rt] = p->sq[rf];
        p->sq[rf] = 0;
    }
    p->ep = kind == CHESS_PAWN && abs_i(to - from) == 16 ? (from + to) / 2 : -1;
    static const struct { int sq, lose; } CORNERS[6] = {{4, CHESS_WK | CHESS_WQ}, {60, CHESS_BK | CHESS_BQ}, {0, CHESS_WQ},
                                                        {7, CHESS_WK}, {56, CHESS_BQ}, {63, CHESS_BK}};
    for (int k = 0; k < 6; k++)
        if (from == CORNERS[k].sq || to == CORNERS[k].sq) p->castle &= ~CORNERS[k].lose;
    if (p->side == CHESS_BLACK) p->fullmove++;
    p->side = !p->side;
}

static int add(int *out, int n, int from, int to) {
    if (n < CHESS_MAX_MOVES) out[n] = CHESS_MOVE(from, to, 0);
    return n + 1;
}

/* the moves before checking whether they leave the king attacked */
static int pseudo_moves(const ChessPos *p, int *out) {
    int n = 0, us = p->side, sign = us == CHESS_WHITE ? 1 : -1;
    for (int s = 0; s < 64; s++) {
        int pc = p->sq[s];
        if (!pc || side_of(pc) != us) continue;
        int f = file_of(s), r = rank_of(s), kind = abs_i(pc);
        if (kind == CHESS_PAWN) {
            int r1 = r + sign, last = us == CHESS_WHITE ? 7 : 0;
            int targets[3], nt = 0;
            if (on_board(f, r1) && !p->sq[CHESS_SQ(f, r1)]) {
                targets[nt++] = CHESS_SQ(f, r1);
                int home = us == CHESS_WHITE ? 1 : 6;
                if (r == home && !p->sq[CHESS_SQ(f, r1 + sign)]) n = add(out, n, s, CHESS_SQ(f, r1 + sign));
            }
            for (int df = -1; df <= 1; df += 2) {
                if (!on_board(f + df, r1)) continue;
                int t = CHESS_SQ(f + df, r1), v = p->sq[t];
                if ((v && side_of(v) != us) || t == p->ep) targets[nt++] = t;
            }
            for (int k = 0; k < nt; k++) {
                if (r1 == last) {
                    for (int pr = CHESS_QUEEN; pr >= CHESS_KNIGHT; pr--)
                        if (n < CHESS_MAX_MOVES) out[n++] = CHESS_MOVE(s, targets[k], pr);
                } else n = add(out, n, s, targets[k]);
            }
        } else if (kind == CHESS_KNIGHT || kind == CHESS_KING) {
            const int(*steps)[2] = kind == CHESS_KNIGHT ? KNIGHT : KING;
            for (int k = 0; k < 8; k++) {
                int nf = f + steps[k][0], nr = r + steps[k][1];
                if (!on_board(nf, nr)) continue;
                int v = p->sq[CHESS_SQ(nf, nr)];
                if (!v || side_of(v) != us) n = add(out, n, s, CHESS_SQ(nf, nr));
            }
        } else {
            int d0 = kind == CHESS_BISHOP ? 4 : 0, d1 = kind == CHESS_ROOK ? 4 : 8;
            for (int d = d0; d < d1; d++) {
                int nf = f + RAYS[d][0], nr = r + RAYS[d][1];
                while (on_board(nf, nr)) {
                    int v = p->sq[CHESS_SQ(nf, nr)];
                    if (v && side_of(v) == us) break;
                    n = add(out, n, s, CHESS_SQ(nf, nr));
                    if (v) break;
                    nf += RAYS[d][0], nr += RAYS[d][1];
                }
            }
        }
    }
    /* castling: the squares between empty, and the king not in, through or into check */
    int home = us == CHESS_WHITE ? 4 : 60, them = !us;
    int ks = us == CHESS_WHITE ? CHESS_WK : CHESS_BK, qs = us == CHESS_WHITE ? CHESS_WQ : CHESS_BQ;
    if ((p->castle & (ks | qs)) && p->sq[home] == sign * CHESS_KING && !chess_attacked(p, home, them)) {
        if ((p->castle & ks) && !p->sq[home + 1] && !p->sq[home + 2] && p->sq[home + 3] == sign * CHESS_ROOK &&
            !chess_attacked(p, home + 1, them) && !chess_attacked(p, home + 2, them))
            n = add(out, n, home, home + 2);
        if ((p->castle & qs) && !p->sq[home - 1] && !p->sq[home - 2] && !p->sq[home - 3] &&
            p->sq[home - 4] == sign * CHESS_ROOK && !chess_attacked(p, home - 1, them) && !chess_attacked(p, home - 2, them))
            n = add(out, n, home, home - 2);
    }
    return n < CHESS_MAX_MOVES ? n : CHESS_MAX_MOVES;
}

int chess_moves(const ChessPos *p, int *out) {
    int all[CHESS_MAX_MOVES], n = pseudo_moves(p, all), k = 0;
    for (int i = 0; i < n; i++) {
        ChessPos q = *p;
        play(&q, all[i]);
        if (!chess_attacked(&q, king_square(&q, p->side), q.side)) out[k++] = all[i];
    }
    return k;
}

int chess_is_legal(const ChessPos *p, int move) {
    int m[CHESS_MAX_MOVES], n = chess_moves(p, m);
    for (int i = 0; i < n; i++)
        if (m[i] == move) return 1;
    return 0;
}

int chess_make(Chess *c, int move) {
    if (chess_state(c) != CHESS_PLAYING || !chess_is_legal(&c->pos, move)) return -1;
    if (c->n == CHESS_HISTORY) { /* the oldest goes: undo reaches back CHESS_HISTORY moves */
        memmove(c->before, c->before + 1, sizeof c->before - sizeof c->before[0]);
        memmove(c->move, c->move + 1, sizeof c->move - sizeof c->move[0]);
        c->n--;
    }
    c->before[c->n] = c->pos;
    c->move[c->n++] = move;
    play(&c->pos, move);
    return 0;
}

int chess_undo(Chess *c) {
    if (!c->n) return -1;
    c->pos = c->before[--c->n];
    return 0;
}

static int same_position(const ChessPos *a, const ChessPos *b) {
    for (int i = 0; i < 64; i++)
        if (a->sq[i] != b->sq[i]) return 0;
    return a->side == b->side && a->castle == b->castle && a->ep == b->ep;
}

static int too_little(const ChessPos *p) { /* kings alone, or with one knight or bishop */
    int minors = 0;
    for (int s = 0; s < 64; s++) {
        int k = abs_i(p->sq[s]);
        if (k == CHESS_PAWN || k == CHESS_ROOK || k == CHESS_QUEEN) return 0;
        if (k == CHESS_KNIGHT || k == CHESS_BISHOP) minors++;
    }
    return minors <= 1;
}

int chess_state(const Chess *c) {
    int m[CHESS_MAX_MOVES];
    if (!chess_moves(&c->pos, m)) return chess_in_check(&c->pos) ? CHESS_CHECKMATE : CHESS_STALEMATE;
    if (c->pos.halfmove >= 100) return CHESS_DRAW_FIFTY;
    int seen = 1;
    for (int i = c->n - 1; i >= 0 && i >= c->n - c->pos.halfmove; i--) seen += same_position(&c->before[i], &c->pos);
    if (seen >= 3) return CHESS_DRAW_REPEAT;
    if (too_little(&c->pos)) return CHESS_DRAW_MATERIAL;
    return CHESS_PLAYING;
}

/* ---- notation ---- */

void chess_san(const ChessPos *p, int move, char out[10]) {
    int from = CHESS_FROM(move), to = CHESS_TO(move), pc = p->sq[from], kind = abs_i(pc);
    int n = 0;
    if (kind == CHESS_KING && abs_i(to - from) == 2) {
        const char *s = to > from ? "O-O" : "O-O-O";
        while (*s) out[n++] = *s++;
    } else {
        int capture = p->sq[to] || (kind == CHESS_PAWN && to == p->ep);
        if (kind != CHESS_PAWN) {
            out[n++] = letter_of(kind);
            int m[CHESS_MAX_MOVES], nm = chess_moves(p, m), same = 0, same_file = 0, same_rank = 0;
            for (int i = 0; i < nm; i++) {
                int o = CHESS_FROM(m[i]);
                if (o == from || CHESS_TO(m[i]) != to || p->sq[o] != pc) continue;
                same = 1;
                same_file |= file_of(o) == file_of(from);
                same_rank |= rank_of(o) == rank_of(from);
            }
            if (same && (!same_file || same_rank)) out[n++] = (char)('a' + file_of(from));
            if (same && same_file) out[n++] = (char)('1' + rank_of(from));
        } else if (capture) out[n++] = (char)('a' + file_of(from));
        if (capture) out[n++] = 'x';
        out[n++] = (char)('a' + file_of(to));
        out[n++] = (char)('1' + rank_of(to));
        if (CHESS_PROMO(move)) out[n++] = '=', out[n++] = letter_of(CHESS_PROMO(move));
    }
    ChessPos q = *p;
    play(&q, move);
    if (chess_in_check(&q)) {
        int m[CHESS_MAX_MOVES];
        out[n++] = chess_moves(&q, m) ? '+' : '#';
    }
    out[n] = 0;
}

/* the text without check marks, = and annotations; O for 0 */
static size_t plain(const char *t, size_t n, char *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++) {
        char ch = t[i];
        if (ch == '+' || ch == '#' || ch == '=' || ch == '!' || ch == '?') continue;
        out[k++] = ch == '0' ? 'O' : ch;
    }
    out[k] = 0;
    return k;
}

int chess_parse(const ChessPos *p, const char *text, size_t n) {
    int m[CHESS_MAX_MOVES], nm = chess_moves(p, m);
    char want[16];
    size_t wn = plain(text, n, want, sizeof want);
    if (!wn || wn >= 12) return -1;
    /* squares: e2e4, e7e8q */
    if ((wn == 4 || wn == 5) && want[0] >= 'a' && want[0] <= 'h' && want[1] >= '1' && want[1] <= '8' && want[2] >= 'a' &&
        want[2] <= 'h' && want[3] >= '1' && want[3] <= '8') {
        int from = CHESS_SQ(want[0] - 'a', want[1] - '1'), to = CHESS_SQ(want[2] - 'a', want[3] - '1'), promo = 0;
        if (wn == 5) {
            promo = abs_i(piece_of_letter(want[4]));
            if (promo < CHESS_KNIGHT || promo > CHESS_QUEEN) return -1;
        }
        for (int i = 0; i < nm; i++)
            if (CHESS_FROM(m[i]) == from && CHESS_TO(m[i]) == to && CHESS_PROMO(m[i]) == promo) return m[i];
        return -1;
    }
    for (int i = 0; i < nm; i++) {
        char san[10], got[16];
        chess_san(p, m[i], san);
        plain(san, strlen(san), got, sizeof got);
        if (same_text(got, want)) return m[i];
    }
    return -1;
}

/* ---- the computer ---- */

static const int WORTH[7] = {0, 100, 320, 330, 500, 900, 0};

/* a piece's worth where it stands: pawns more as they go, knights and
 * bishops toward the middle, the king home while there are queens */
static int placed(int kind, int s, int side, int endgame) {
    int f = file_of(s), r = side == CHESS_WHITE ? rank_of(s) : 7 - rank_of(s);
    int centre = 7 - (abs_i(2 * f - 7) + abs_i(2 * rank_of(s) - 7)) / 2; /* 0 at a corner, 6 in the middle */
    switch (kind) {
    case CHESS_PAWN: return r * r * 2 + (f >= 2 && f <= 5 ? r * 2 : 0);
    case CHESS_KNIGHT: return centre * 6 - 12;
    case CHESS_BISHOP: return centre * 3;
    case CHESS_ROOK: return r == 6 ? 15 : 0;
    case CHESS_QUEEN: return centre;
    case CHESS_KING: return endgame ? centre * 5 : (r == 0 ? 10 : -10 * r) + (f == 1 || f == 6 ? 15 : 0);
    }
    return 0;
}

static int evaluate(const ChessPos *p) { /* for the side to move */
    int score = 0, heavy = 0;
    for (int s = 0; s < 64; s++)
        if (abs_i(p->sq[s]) == CHESS_QUEEN) heavy++;
    int endgame = !heavy;
    for (int s = 0; s < 64; s++) {
        int pc = p->sq[s];
        if (!pc) continue;
        int v = WORTH[abs_i(pc)] + placed(abs_i(pc), s, side_of(pc), endgame);
        score += pc > 0 ? v : -v;
    }
    return p->side == CHESS_WHITE ? score : -score;
}

#define MATE 100000

/* captures and promotions first, the biggest taken by the smallest first */
static void order(const ChessPos *p, int *m, int n) {
    int key[CHESS_MAX_MOVES];
    for (int i = 0; i < n; i++) {
        int victim = abs_i(p->sq[CHESS_TO(m[i])]), by = abs_i(p->sq[CHESS_FROM(m[i])]);
        key[i] = (victim ? WORTH[victim] * 10 - WORTH[by] / 10 + 1000 : 0) + (CHESS_PROMO(m[i]) ? WORTH[CHESS_PROMO(m[i])] : 0);
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && key[j] > key[j - 1]; j--) {
            int t = key[j];
            key[j] = key[j - 1], key[j - 1] = t;
            t = m[j];
            m[j] = m[j - 1], m[j - 1] = t;
        }
}

/* only captures, until it is quiet */
static int quiet(const ChessPos *p, int alpha, int beta, int depth) {
    int stand = evaluate(p);
    if (stand >= beta || depth == 0) return stand;
    if (stand > alpha) alpha = stand;
    int m[CHESS_MAX_MOVES], n = chess_moves(p, m), k = 0;
    for (int i = 0; i < n; i++)
        if (p->sq[CHESS_TO(m[i])] || CHESS_PROMO(m[i]) == CHESS_QUEEN) m[k++] = m[i];
    order(p, m, k);
    for (int i = 0; i < k; i++) {
        ChessPos q = *p;
        play(&q, m[i]);
        int v = -quiet(&q, -beta, -alpha, depth - 1);
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

static int search(const ChessPos *p, int depth, int alpha, int beta, int ply) {
    int m[CHESS_MAX_MOVES], n = chess_moves(p, m);
    if (!n) return chess_in_check(p) ? -MATE + ply : 0;
    if (p->halfmove >= 100) return 0;
    if (depth == 0) return quiet(p, alpha, beta, 6);
    order(p, m, n);
    int best = -MATE * 2;
    for (int i = 0; i < n; i++) {
        ChessPos q = *p;
        play(&q, m[i]);
        int v = -search(&q, depth - 1, -beta, -alpha, ply + 1);
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

static uint32_t mix(uint32_t z) {
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

int chess_ai(const Chess *c) {
    const ChessPos *p = &c->pos;
    int m[CHESS_MAX_MOVES], n = chess_moves(p, m);
    if (!n) return -1;
    order(p, m, n);
    int depth = c->level < 1 ? 1 : c->level > 4 ? 4 : c->level;
    int best = -1, best_v = -MATE * 2;
    for (int i = 0; i < n; i++) {
        ChessPos q = *p;
        play(&q, m[i]);
        int v = -search(&q, depth - 1, -MATE * 2, -best_v + 8, 1);
        /* a few points either way from the game's seed: not the same game every time */
        v += (int)(mix(c->seed * 0x9E3779B9u + (uint32_t)c->n * 977u + (uint32_t)m[i]) % 8u);
        /* a position seen before counts as a draw: no walking into repetition when ahead */
        for (int k = c->n - 1; k >= 0 && k >= c->n - p->halfmove - 1; k--)
            if (same_position(&c->before[k], &q)) {
                v = 0;
                break;
            }
        if (v > best_v) best_v = v, best = m[i];
    }
    return best;
}

int chess_run(Chess *c) {
    int k = 0;
    while (chess_state(c) == CHESS_PLAYING && !c->human[c->pos.side]) {
        int m = chess_ai(c);
        if (m < 0 || chess_make(c, m)) break;
        k++;
    }
    return k;
}

/* ---- text ---- */

typedef struct {
    char *out;
    size_t cap, len;
} Text;

static void put(Text *t, const char *s) {
    for (; *s; s++, t->len++)
        if (t->len + 1 < t->cap) t->out[t->len] = *s;
}

static void put_num(Text *t, long v) {
    char b[24], r[24];
    int n = 0;
    unsigned long u = v < 0 ? 0 : (unsigned long)v;
    do b[n++] = (char)('0' + u % 10);
    while (u /= 10);
    for (int i = 0; i < n; i++) r[i] = b[n - 1 - i];
    r[n] = 0;
    put(t, r);
}

size_t chess_text(const Chess *c, char *out, size_t cap) {
    static const char *const STATE[6] = {"", "checkmate", "stalemate", "a draw by the fifty-move rule",
                                         "a draw by repetition", "a draw: too little to mate"};
    Text t = {out, cap, 0};
    const ChessPos *p = &c->pos;
    int st = chess_state(c);
    put(&t, "Chess  game ");
    put_num(&t, (long)c->seed);
    put(&t, "  level ");
    put_num(&t, c->level);
    put(&t, c->human[0] && c->human[1] ? "  two players  " : c->human[0] ? "  you white  " : c->human[1] ? "  you black  " : "  computer  ");
    if (st == CHESS_PLAYING) {
        put(&t, p->side == CHESS_WHITE ? "white to move" : "black to move");
        if (chess_in_check(p)) put(&t, ", check");
    } else {
        put(&t, STATE[st]);
        if (st == CHESS_CHECKMATE) put(&t, p->side == CHESS_WHITE ? ": black wins" : ": white wins");
    }
    put(&t, "\n  +-----------------+\n");
    for (int r = 7; r >= 0; r--) {
        char row[32] = "8 | . . . . . . . . |\n";
        row[0] = (char)('1' + r);
        for (int f = 0; f < 8; f++) row[4 + 2 * f] = letter_of(p->sq[CHESS_SQ(f, r)]);
        put(&t, row);
    }
    put(&t, "  +-----------------+\n    a b c d e f g h\n");
    put(&t, "Moves");
    for (int i = 0; i < c->n; i++) {
        const ChessPos *b = &c->before[i];
        char san[10];
        if (b->side == CHESS_WHITE || i == 0) {
            put(&t, " ");
            put_num(&t, b->fullmove);
            put(&t, b->side == CHESS_WHITE ? "." : "...");
        }
        chess_san(b, c->move[i], san);
        put(&t, " ");
        put(&t, san);
    }
    put(&t, "\nFEN ");
    char fen[100];
    chess_fen(p, fen, sizeof fen);
    put(&t, fen);
    put(&t, "\n");
    if (cap) out[t.len < cap ? t.len : cap - 1] = 0;
    return t.len;
}

/* ---- commands ---- */

static void say(char *msg, size_t cap, const char *a) {
    if (!cap) return;
    size_t n = 0;
    for (; a[n] && n + 1 < cap; n++) msg[n] = a[n];
    msg[n] = 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static size_t word(const char **p, const char **start) {
    while (**p == ' ' || **p == '\t') (*p)++;
    *start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    return (size_t)(*p - *start);
}

static int is(const char *w, size_t n, const char *s) {
    size_t k = 0;
    for (; k < n && s[k]; k++)
        if (lower(w[k]) != s[k]) return 0;
    return k == n && !s[k];
}

static int number(const char *w, size_t n, uint32_t *v) {
    if (!n || n > 10) return -1;
    uint64_t x = 0;
    for (size_t i = 0; i < n; i++) {
        if (w[i] < '0' || w[i] > '9') return -1;
        x = x * 10 + (uint64_t)(w[i] - '0');
    }
    if (x > 0xFFFFFFFFu) return -1;
    *v = (uint32_t)x;
    return 0;
}

static const char *over_why(int st) {
    return st == CHESS_CHECKMATE ? "the game is over: checkmate" : st == CHESS_STALEMATE ? "the game is over: stalemate"
                                                                                       : "the game is over: a draw";
}

int chess_command(Chess *c, const char *line, char *msg, size_t cap) {
    const char *p = line, *w[4], *e;
    size_t n[4];
    int nw = 0;
    say(msg, cap, "");
    w[0] = NULL;
    n[0] = word(&p, &w[0]);
    if (!n[0]) return 0;
    if (is(w[0], n[0], "fen")) {
        int h0 = c->human[0], h1 = c->human[1];
        if (chess_set_fen(c, p)) {
            say(msg, cap, "fen: not a position (a king each, the side not to move not in check)");
            return -1;
        }
        c->human[0] = h0, c->human[1] = h1;
        chess_run(c);
        return 0;
    }
    nw = 1;
    while (nw < 4 && (n[nw] = word(&p, &w[nw]))) nw++;
    if (word(&p, &e)) {
        say(msg, cap, "too many words");
        return -1;
    }
    if (is(w[0], n[0], "new")) {
        int side = 0, k = 1;
        uint32_t seed = c->seed + 1;
        if (k < nw && (is(w[k], n[k], "white") || is(w[k], n[k], "black") || is(w[k], n[k], "both")))
            side = is(w[k], n[k], "white") ? 0 : is(w[k], n[k], "black") ? 1 : 2, k++;
        if (k < nw && !number(w[k], n[k], &seed)) k++;
        if (k != nw) {
            say(msg, cap, "new [white|black|both] [SEED]");
            return -1;
        }
        int level = c->level;
        chess_new(c, seed);
        c->level = level;
        c->human[0] = side != 1;
        c->human[1] = side != 0;
    } else if (is(w[0], n[0], "move")) {
        int st = chess_state(c);
        if (nw != 2) {
            say(msg, cap, "move M: as Nf3, exd5, O-O, e8=Q or e2e4");
            return -1;
        }
        if (st != CHESS_PLAYING) {
            say(msg, cap, over_why(st));
            return -1;
        }
        int m = chess_parse(&c->pos, w[1], n[1]);
        if (m < 0) {
            say(msg, cap, "not a legal move here");
            return -1;
        }
        chess_make(c, m);
    } else if (is(w[0], n[0], "undo")) {
        if (nw != 1 || !c->n) {
            say(msg, cap, "nothing to undo");
            return -1;
        }
        chess_undo(c);
        while (c->n && !c->human[c->pos.side]) chess_undo(c); /* back to a person's turn */
        return 0;
    } else if (is(w[0], n[0], "go")) {
        int st = chess_state(c);
        if (nw != 1 || st != CHESS_PLAYING) {
            say(msg, cap, st != CHESS_PLAYING ? over_why(st) : "go: no words after it");
            return -1;
        }
        chess_make(c, chess_ai(c));
    } else if (is(w[0], n[0], "level")) {
        uint32_t v;
        if (nw != 2 || number(w[1], n[1], &v) || v < 1 || v > 4) {
            say(msg, cap, "level N: 1 to 4");
            return -1;
        }
        c->level = (int)v;
        return 0;
    } else {
        say(msg, cap, "unknown: try new, move, undo, go, level, fen");
        return -1;
    }
    chess_run(c);
    return 0;
}

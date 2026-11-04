/*
 * sqqr image layer: find a QR symbol in a grayscale image and sample its module
 * grid for sqqr_decode_grid.
 *
 * Original code. The steps follow the public symbol structure (ISO/IEC 18004):
 * finder patterns have a 1:1:3:1:1 dark/light profile through their centre,
 * the alignment pattern is a 5x5 ring around a single dark module, and the
 * symbol is a plane, so four point pairs fix a perspective mapping.
 *
 *   1. Binarize with a threshold taken per 8x8 block from the local range.
 *   2. Scan rows for 1:1:3:1:1 runs, confirm each hit across the column and
 *      the row again, and merge hits that land on the same pattern.
 *   3. Pick the three finders that best form an isosceles right angle.
 *   4. Estimate the size, look for the bottom-right alignment pattern, map
 *      module space to the image with a homography and sample every module.
 *   5. Decode; on failure try the neighbouring sizes, other finder triples and
 *      the inverted image (light-on-dark symbols).
 */
#include "sqqr.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef SQQR_LOCATE_DEBUG
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

#define BLOCK 8
#define MAX_FINDERS 256 /* hits, including look-alikes in the data area */
#define MAX_CANDIDATES 16 /* strongest hits considered for triples */
#define MAX_TRIPLES 6

typedef struct { double x, y; } Pt;
typedef struct { double x, y, m; int count; } Finder;

typedef struct {
    uint8_t *bin; /* 1 = dark */
    int w, h;
    Finder f[MAX_FINDERS];
    int nf;
} Scan;

static int dark(const Scan *s, int x, int y) {
    return x >= 0 && y >= 0 && x < s->w && y < s->h && s->bin[(size_t)y * (size_t)s->w + (size_t)x];
}

/* ---- 1. binarize ----------------------------------------------------------- */

static void binarize(const uint8_t *g, int w, int h, int stride, uint8_t *bin, uint8_t *st) {
    int bw = (w + BLOCK - 1) / BLOCK, bh = (h + BLOCK - 1) / BLOCK;
    size_t nb = (size_t)bw * (size_t)bh;
    uint8_t *mean = st, *mn = st + nb, *mx = st + 2 * nb;
    int gmin = 255, gmax = 0;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            int lo = 255, hi = 0;
            unsigned sum = 0, cnt = 0;
            for (int y = by * BLOCK; y < by * BLOCK + BLOCK && y < h; y++)
                for (int x = bx * BLOCK; x < bx * BLOCK + BLOCK && x < w; x++) {
                    int v = g[(size_t)y * (size_t)stride + (size_t)x];
                    sum += (unsigned)v;
                    cnt++;
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
            size_t i = (size_t)by * (size_t)bw + (size_t)bx;
            mean[i] = (uint8_t)(sum / cnt);
            mn[i] = (uint8_t)lo;
            mx[i] = (uint8_t)hi;
            if (lo < gmin) gmin = lo;
            if (hi > gmax) gmax = hi;
        }
    int gmid = (gmin + gmax) / 2;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            int lo = 255, hi = 0;
            unsigned sum = 0, cnt = 0;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int x = bx + dx, y = by + dy;
                    if (x < 0 || y < 0 || x >= bw || y >= bh) continue;
                    size_t i = (size_t)y * (size_t)bw + (size_t)x;
                    sum += mean[i];
                    cnt++;
                    if (mn[i] < lo) lo = mn[i];
                    if (mx[i] > hi) hi = mx[i];
                }
            int thr;
            if (hi - lo < 24) /* flat neighbourhood: all one colour */
                thr = (int)(sum / cnt) < gmid ? 256 : 0;
            else
                thr = (lo + hi) / 2;
            for (int y = by * BLOCK; y < by * BLOCK + BLOCK && y < h; y++)
                for (int x = bx * BLOCK; x < bx * BLOCK + BLOCK && x < w; x++)
                    bin[(size_t)y * (size_t)w + (size_t)x] = g[(size_t)y * (size_t)stride + (size_t)x] < thr;
        }
}

/* ---- 2. finder patterns ------------------------------------------------------ */

static int ratio_ok(const int r[5]) {
    int total = r[0] + r[1] + r[2] + r[3] + r[4];
    if (total < 7) return 0;
    double m = total / 7.0, tol = m * 0.6;
    return fabs(r[0] - m) < tol && fabs(r[1] - m) < tol && fabs(r[2] - 3 * m) < 3 * tol &&
           fabs(r[3] - m) < tol && fabs(r[4] - m) < tol;
}

/* Walk through (x, y) along (dx, dy) and check the 1:1:3:1:1 profile. On success
 * returns 1 and the centre coordinate along the walk (continuous, pixel i spans
 * [i, i+1)) and the module size. */
static int cross_check(const Scan *s, int x, int y, int dx, int dy, double expect, double *centre, double *module) {
    if (!dark(s, x, y)) return 0;
    int lim = (int)(expect * 1.5) + 2;
    int r[5] = {0, 0, 0, 0, 0};
    int a = 0, b = 0, k;
    for (k = 0; dark(s, x - dx * k, y - dy * k) && k <= lim; k++) a++;
    for (; !dark(s, x - dx * k, y - dy * k) && r[1] <= lim && x - dx * k >= 0 && y - dy * k >= 0; k++) r[1]++;
    for (; dark(s, x - dx * k, y - dy * k) && r[0] <= lim; k++) r[0]++;
    for (k = 1; dark(s, x + dx * k, y + dy * k) && k <= lim; k++) b++;
    for (; !dark(s, x + dx * k, y + dy * k) && r[3] <= lim && x + dx * k < s->w && y + dy * k < s->h; k++) r[3]++;
    for (; dark(s, x + dx * k, y + dy * k) && r[4] <= lim; k++) r[4]++;
    r[2] = a + b;
    if (!r[0] || !r[1] || !r[3] || !r[4] || !ratio_ok(r)) return 0;
    int total = r[0] + r[1] + r[2] + r[3] + r[4];
    if (fabs(total - expect) > expect * 0.5) return 0;
    *centre = (dx ? x : y) + (b - a + 1) / 2.0 + 0.5;
    *module = total / 7.0;
    return 1;
}

static void add_finder(Scan *s, double x, double y, double m) {
    for (int i = 0; i < s->nf; i++) {
        Finder *f = &s->f[i];
        if (fabs(f->x - x) <= f->m * 2 && fabs(f->y - y) <= f->m * 2 && fabs(f->m - m) <= f->m * 0.6 + 1) {
            double c = f->count;
            f->x = (f->x * c + x) / (c + 1);
            f->y = (f->y * c + y) / (c + 1);
            f->m = (f->m * c + m) / (c + 1);
            f->count++;
            return;
        }
    }
    if (s->nf < MAX_FINDERS) s->f[s->nf++] = (Finder){x, y, m, 1};
}

static void found_run(Scan *s, const int r[5], int end_x, int y) {
    int total = r[0] + r[1] + r[2] + r[3] + r[4];
    double cx = end_x - r[4] - r[3] - r[2] / 2.0, cy, mv, cx2, mh;
    if (!cross_check(s, (int)cx, y, 0, 1, total, &cy, &mv)) return;
    if (!cross_check(s, (int)cx, (int)cy, 1, 0, total, &cx2, &mh)) return;
    /* The diagonal must show the pattern too; this rejects most text and edges. */
    double cd, md;
    if (!cross_check(s, (int)cx2, (int)cy, 1, 1, total, &cd, &md)) return;
    add_finder(s, cx2, cy, (mv + mh) / 2);
}

static void find_finders(Scan *s) {
    s->nf = 0;
    for (int y = 0; y < s->h; y++) {
        const uint8_t *row = s->bin + (size_t)y * (size_t)s->w;
        int r[5] = {0, 0, 0, 0, 0}, nruns = 0, cur = 0, color = row[0];
        for (int x = 0; x <= s->w; x++) {
            int c = x < s->w ? row[x] : !color;
            if (c == color) {
                cur++;
                continue;
            }
            memmove(r, r + 1, 4 * sizeof r[0]);
            r[4] = cur;
            nruns++;
            if (color && nruns >= 5 && ratio_ok(r)) found_run(s, r, x, y);
            color = c;
            cur = 1;
        }
    }
}

/* ---- 3. pick three finders --------------------------------------------------- */

typedef struct { int tl, tr, bl; double score; } Triple;

static double dist(Pt a, Pt b) { return hypot(a.x - b.x, a.y - b.y); }
static Pt fpt(const Finder *f) { return (Pt){f->x, f->y}; }

static int pick_triples(const Scan *s, Triple *out) {
    /* The strongest hits (most rows confirmed) first; a real finder is
     * confirmed on about 3 modules' worth of rows, a look-alike on few. */
    int idx[MAX_CANDIDATES], n = 0;
    for (int i = 0; i < s->nf; i++) {
        int c = s->f[i].count, pos = n < MAX_CANDIDATES ? n++ : MAX_CANDIDATES;
        if (pos == MAX_CANDIDATES) {
            if (c <= s->f[idx[MAX_CANDIDATES - 1]].count) continue;
            pos = MAX_CANDIDATES - 1;
        }
        while (pos > 0 && s->f[idx[pos - 1]].count < c) {
            idx[pos] = idx[pos - 1];
            pos--;
        }
        idx[pos] = i;
    }
    /* Drop look-alikes confirmed on far fewer rows than the best hit. */
    while (n > 3 && s->f[idx[n - 1]].count * 4 < s->f[idx[0]].count) n--;
    int nt = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            for (int k = j + 1; k < n; k++) {
                int v[3] = {idx[i], idx[j], idx[k]};
                const Finder *f[3] = {&s->f[v[0]], &s->f[v[1]], &s->f[v[2]]};
                double mmin = f[0]->m, mmax = f[0]->m;
                for (int q = 1; q < 3; q++) {
                    if (f[q]->m < mmin) mmin = f[q]->m;
                    if (f[q]->m > mmax) mmax = f[q]->m;
                }
                if (mmax > mmin * 1.8) continue;
                /* The corner is opposite the longest side. */
                double d01 = dist(fpt(f[0]), fpt(f[1])), d02 = dist(fpt(f[0]), fpt(f[2])),
                       d12 = dist(fpt(f[1]), fpt(f[2]));
                int c = d12 >= d01 && d12 >= d02 ? 0 : d02 >= d01 ? 1 : 2;
                int o1 = (c + 1) % 3, o2 = (c + 2) % 3;
                Pt pc = fpt(f[c]), p1 = fpt(f[o1]), p2 = fpt(f[o2]);
                double l1 = dist(pc, p1), l2 = dist(pc, p2), m = (f[0]->m + f[1]->m + f[2]->m) / 3;
                if (l1 < 8 * m || l2 < 8 * m) continue; /* >= 14 modules apart; m may read sqrt 2 high */
                double cosang = ((p1.x - pc.x) * (p2.x - pc.x) + (p1.y - pc.y) * (p2.y - pc.y)) / (l1 * l2);
                double score = fabs(l1 - l2) / (l1 > l2 ? l1 : l2) + fabs(cosang) + (mmax - mmin) / mmax;
                if (score > 0.7) continue;
                /* y points down: (tr - tl) x (bl - tl) > 0 in standard orientation. */
                double cross = (p1.x - pc.x) * (p2.y - pc.y) - (p1.y - pc.y) * (p2.x - pc.x);
                Triple t = {v[c], cross > 0 ? v[o1] : v[o2], cross > 0 ? v[o2] : v[o1], score};
                int pos = nt < MAX_TRIPLES ? nt++ : MAX_TRIPLES;
                if (pos == MAX_TRIPLES) {
                    if (score >= out[MAX_TRIPLES - 1].score) continue;
                    pos = MAX_TRIPLES - 1;
                }
                while (pos > 0 && out[pos - 1].score > score) {
                    out[pos] = out[pos - 1];
                    pos--;
                }
                out[pos] = t;
            }
    return nt;
}

/* ---- 4. geometry and sampling -------------------------------------------- */

/* Least-squares homography from k >= 4 pairs (u, v) -> (x, y), as a row-major
 * 3x3 matrix. Both point sets are normalized first (centroid at the origin,
 * mean distance sqrt 2) so the normal equations stay well conditioned. */
static void norm_xform(const Pt *p, int k, double t[3]) { /* scale, mx, my */
    double mx = 0, my = 0, md = 0;
    for (int i = 0; i < k; i++) mx += p[i].x, my += p[i].y;
    mx /= k;
    my /= k;
    for (int i = 0; i < k; i++) md += hypot(p[i].x - mx, p[i].y - my);
    md /= k;
    t[0] = md > 1e-9 ? 1.4142135623730951 / md : 1;
    t[1] = mx;
    t[2] = my;
}

static int fit_h(const Pt *src, const Pt *dst, int k, double hm[9]) {
    double ts[3], td[3], ata[8][9];
    norm_xform(src, k, ts);
    norm_xform(dst, k, td);
    memset(ata, 0, sizeof ata);
    for (int i = 0; i < k; i++) {
        double u = (src[i].x - ts[1]) * ts[0], v = (src[i].y - ts[2]) * ts[0];
        double x = (dst[i].x - td[1]) * td[0], y = (dst[i].y - td[2]) * td[0];
        double rows[2][9] = {{u, v, 1, 0, 0, 0, -u * x, -v * x, x}, {0, 0, 0, u, v, 1, -u * y, -v * y, y}};
        for (int r = 0; r < 2; r++)
            for (int a = 0; a < 8; a++)
                for (int b = 0; b < 9; b++) ata[a][b] += rows[r][a] * rows[r][b];
    }
    for (int c = 0; c < 8; c++) {
        int p = c;
        for (int r = c + 1; r < 8; r++)
            if (fabs(ata[r][c]) > fabs(ata[p][c])) p = r;
        if (fabs(ata[p][c]) < 1e-12) return 0;
        if (p != c)
            for (int q = 0; q < 9; q++) {
                double t = ata[c][q];
                ata[c][q] = ata[p][q];
                ata[p][q] = t;
            }
        for (int r = 0; r < 8; r++) {
            if (r == c) continue;
            double f = ata[r][c] / ata[c][c];
            for (int q = c; q < 9; q++) ata[r][q] -= f * ata[c][q];
        }
    }
    double hn[9];
    for (int i = 0; i < 8; i++) hn[i] = ata[i][8] / ata[i][i];
    hn[8] = 1;
    /* hm = inv(Td) * hn * Ts, with T = [s 0 -s*mx; 0 s -s*my; 0 0 1]. */
    double ts_m[9] = {ts[0], 0, -ts[0] * ts[1], 0, ts[0], -ts[0] * ts[2], 0, 0, 1};
    double tdi[9] = {1 / td[0], 0, td[1], 0, 1 / td[0], td[2], 0, 0, 1};
    double tmp[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            tmp[r * 3 + c] = 0;
            for (int q = 0; q < 3; q++) tmp[r * 3 + c] += hn[r * 3 + q] * ts_m[q * 3 + c];
        }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            hm[r * 3 + c] = 0;
            for (int q = 0; q < 3; q++) hm[r * 3 + c] += tdi[r * 3 + q] * tmp[q * 3 + c];
        }
    return fabs(hm[8]) > 1e-12;
}

static Pt apply(const double hm[9], double u, double v) {
    double d = hm[6] * u + hm[7] * v + hm[8];
    return (Pt){(hm[0] * u + hm[1] * v + hm[2]) / d, (hm[3] * u + hm[4] * v + hm[5]) / d};
}

static int score_alignment(const Scan *s, double cx, double cy, Pt ux, Pt uy) {
    int score = 0;
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++) {
            int ring = abs(i) > abs(j) ? abs(i) : abs(j);
            double x = cx + ux.x * i + uy.x * j, y = cy + ux.y * i + uy.y * j;
            score += dark(s, (int)floor(x), (int)floor(y)) == (ring != 1);
        }
    return score;
}

/* Look for an alignment pattern within rad_mod modules of est: a dark module,
 * a light ring and a dark ring, sampled along the module vectors ux, uy. On
 * success returns 1 and the centre (average of the best-scoring positions next
 * to the one nearest est). */
static int find_alignment(const Scan *s, Pt est, Pt ux, Pt uy, double rad_mod, Pt *out) {
    double m = (hypot(ux.x, ux.y) + hypot(uy.x, uy.y)) / 2;
    if (!(m >= 1 && m <= 64)) return 0; /* a degenerate model */
    int rad = (int)(m * rad_mod) + 2, best = -1;
    double best_d = 0;
    Pt anchor = est;
    for (int oy = -rad; oy <= rad; oy++)
        for (int ox = -rad; ox <= rad; ox++) {
            int sc = score_alignment(s, est.x + ox, est.y + oy, ux, uy);
            double d = (double)ox * ox + (double)oy * oy;
            if (sc > best || (sc == best && d < best_d)) {
                best = sc;
                best_d = d;
                anchor = (Pt){est.x + ox, est.y + oy};
            }
        }
    if (best < 23) return 0;
    double sx = 0, sy = 0;
    int cnt = 0, r2 = (int)m + 1;
    for (int oy = -r2; oy <= r2; oy++)
        for (int ox = -r2; ox <= r2; ox++)
            if (score_alignment(s, anchor.x + ox, anchor.y + oy, ux, uy) == best) {
                sx += anchor.x + ox;
                sy += anchor.y + oy;
                cnt++;
            }
    *out = (Pt){sx / cnt, sy / cnt};
    return 1;
}

/* Module space -> image. A least-squares homography, plus the residuals at the
 * points it was fitted to (finder and alignment centres), spread by inverse
 * distance weighting. A real lens or a curved page is not one plane, and the
 * residuals carry that local bend into the sampling. */
#define MAX_POINTS (3 + 49)
typedef struct {
    double hm[9];
    int k;
    Pt src[MAX_POINTS], res[MAX_POINTS];
} Model;

static void model_residuals(Model *md, const Pt *dst) {
    for (int i = 0; i < md->k; i++) {
        Pt p = apply(md->hm, md->src[i].x, md->src[i].y);
        md->res[i] = (Pt){dst[i].x - p.x, dst[i].y - p.y};
    }
}

static Pt model_apply(const Model *md, double u, double v) {
    Pt p = apply(md->hm, u, v);
    if (md->k <= 4) return p; /* an exact fit: no residuals */
    double sw = 0, rx = 0, ry = 0;
    for (int i = 0; i < md->k; i++) {
        double du = u - md->src[i].x, dv = v - md->src[i].y, d2 = du * du + dv * dv + 1;
        double w = 1 / (d2 * d2);
        sw += w;
        rx += w * md->res[i].x;
        ry += w * md->res[i].y;
    }
    return (Pt){p.x + rx / sw, p.y + ry / sw};
}

static int sample_and_decode(const Scan *s, const Model *md, int n, uint8_t *grid, SqqrResult *out) {
    /* Vote over the module centre and four points a quarter module away, so one
     * noisy pixel or a slightly-off centre doesn't flip a module. */
    static const double off[5][2] = {{0, 0}, {-0.25, 0}, {0.25, 0}, {0, -0.25}, {0, 0.25}};
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++) {
            double u = c + 0.5, v = r + 0.5;
            Pt pc = model_apply(md, u, v), h0 = apply(md->hm, u, v);
            int votes = 0;
            for (int k = 0; k < 5; k++) {
                Pt h = apply(md->hm, u + off[k][0], v + off[k][1]);
                votes += dark(s, (int)floor(pc.x + h.x - h0.x), (int)floor(pc.y + h.y - h0.y)) * (k ? 1 : 2);
            }
            grid[r * n + c] = (uint8_t)(votes >= 3);
        }
    return sqqr_decode_grid(grid, n, out);
}

/* Map module space to the image for an n x n symbol. Starts from the three
 * finder centres (an affine guess), then finds alignment patterns from the
 * top-left outwards, refitting after each one, so later searches start from a
 * better prediction. Returns the number of alignment patterns used. */
static int fit_symbol(const Scan *s, Pt tl, Pt tr, Pt bl, int n, Model *md) {
    Pt dst[MAX_POINTS];
    md->k = 3;
    md->src[0] = (Pt){3.5, 3.5};
    md->src[1] = (Pt){n - 3.5, 3.5};
    md->src[2] = (Pt){3.5, n - 3.5};
    dst[0] = tl;
    dst[1] = tr;
    dst[2] = bl;
    /* Affine start: the fourth point completes the parallelogram. */
    Pt s4[4] = {md->src[0], md->src[1], md->src[2], {n - 3.5, n - 3.5}};
    Pt d4[4] = {tl, tr, bl, {tr.x + bl.x - tl.x, tr.y + bl.y - tl.y}};
    if (!fit_h(s4, d4, 4, md->hm)) return -1;
    model_residuals(md, dst);
    int pos[7], np = sqqr_alignment_positions((n - 17) / 4, pos);
    for (int ring = 1; ring < np; ring++)
        for (int t = 0; t <= 2 * ring; t++) {
            /* Walk the L-shaped ring max(i, j) == ring: (ring, 0..ring), then (ring-1..0, ring). */
            int i = t <= ring ? ring : 2 * ring - t, j = t <= ring ? t : ring;
            if ((i == np - 1 && j == 0) || (i == 0 && j == np - 1)) continue; /* finders */
            double u = pos[i] + 0.5, v = pos[j] + 0.5;
            Pt est = model_apply(md, u, v), p0 = apply(md->hm, u, v), px = apply(md->hm, u + 1, v),
               py = apply(md->hm, u, v + 1);
            Pt ux = {px.x - p0.x, px.y - p0.y}, uy = {py.x - p0.x, py.y - p0.y}, found;
            if (!find_alignment(s, est, ux, uy, md->k < 5 ? 4.0 : 2.5, &found)) continue;
            md->src[md->k] = (Pt){u, v};
            dst[md->k] = found;
            md->k++;
            if (md->k >= 4) {
                double next[9];
                if (fit_h(md->src, dst, md->k, next)) memcpy(md->hm, next, sizeof next);
            }
            model_residuals(md, dst);
        }
    return md->k - 3;
}

/* Width of the finder at c measured along the unit vector d (both ways), in
 * pixels per module, or 0 if the 1:1:3:1:1 profile isn't there. Axis-aligned
 * runs overestimate the module of a rotated symbol by up to sqrt(2). */
static double module_along(const Scan *s, Pt c, Pt d, double m_guess) {
    int lim = (int)(m_guess * 6) + 4, r[5] = {0, 0, 0, 0, 0}, k;
    for (int side = -1; side <= 1; side += 2) {
        int *inner = &r[2], *gap = side < 0 ? &r[1] : &r[3], *outer = side < 0 ? &r[0] : &r[4];
        k = side < 0 ? 0 : 1;
#define AT(kk) dark(s, (int)floor(c.x + side * d.x * (kk)), (int)floor(c.y + side * d.y * (kk)))
        for (; AT(k) && k <= lim; k++) (*inner)++;
        for (; !AT(k) && *gap <= lim; k++) (*gap)++;
        for (; AT(k) && *outer <= lim; k++) (*outer)++;
#undef AT
    }
    if (!r[0] || !r[1] || !r[3] || !r[4] || !ratio_ok(r)) return 0;
    return (r[0] + r[1] + r[2] + r[3] + r[4]) / 7.0;
}

/* Version 7+ symbols carry their version next to the top-right and bottom-left
 * finders. Both blocks sit at fixed offsets from their finder centre, so they
 * can be read with the finder's local module vectors before the size is known.
 * Returns the size, or 0. */
static int size_from_version_info(const Scan *s, Pt tr, Pt bl, Pt tux, Pt tuy, Pt bux, Pt buy) {
    for (int block = 0; block < 2; block++) {
        uint32_t word = 0;
        for (int i = 17; i >= 0; i--) {
            /* Top-right: row i/3, col n-11+i%3; bottom-left is the transpose. */
            double a = i % 3 - 7, b = i / 3 - 3;
            Pt p = block == 0 ? (Pt){tr.x + tux.x * a + tuy.x * b, tr.y + tux.y * a + tuy.y * b}
                              : (Pt){bl.x + bux.x * b + buy.x * a, bl.y + bux.y * b + buy.y * a};
            word = (word << 1) | (uint32_t)dark(s, (int)floor(p.x), (int)floor(p.y));
        }
        int v = sqqr_decode_version_bits(word);
        if (v >= 7) return 17 + 4 * v;
    }
    return 0;
}

static double b_or(double v, double fallback) { return v > 0 ? v : fallback; }

static int try_triple(const Scan *s, const Triple *t, uint8_t *grid, SqqrResult *out) {
    const Finder *ftl = &s->f[t->tl], *ftr = &s->f[t->tr], *fbl = &s->f[t->bl];
    Pt tl = fpt(ftl), tr = fpt(ftr), bl = fpt(fbl);
    double m = (ftl->m + ftr->m + fbl->m) / 3, lx = dist(tl, tr), ly = dist(tl, bl);
    Pt dx = {(tr.x - tl.x) / lx, (tr.y - tl.y) / lx}, dy = {(bl.x - tl.x) / ly, (bl.y - tl.y) / ly};
    double a = module_along(s, tl, dx, m), b = module_along(s, tr, dx, m);
    double c = module_along(s, tl, dy, m), d = module_along(s, bl, dy, m);
    double mx = a && b ? (a + b) / 2 : a ? a : b ? b : m, my = c && d ? (c + d) / 2 : c ? c : d ? d : m;
    double est = (lx / mx + ly / my) / 2 + 7;
    int n0 = (int)lround(est);
    switch (n0 & 3) {
    case 0: n0 += 1; break;
    case 2: n0 -= 1; break;
    case 3: n0 += 2; break;
    default: break;
    }
    DBG("  triple tl(%.1f,%.1f m%.2f c%d) tr(%.1f,%.1f m%.2f c%d) bl(%.1f,%.1f m%.2f c%d) score %.3f n0 %d (est %.2f)\n",
        tl.x, tl.y, ftl->m, ftl->count, tr.x, tr.y, ftr->m, ftr->count, bl.x, bl.y, fbl->m, fbl->count, t->score, n0, est);
    int sizes[6], ns = 0, nv = 0;
    if (n0 >= 37) {
        double e = module_along(s, tr, dy, m), f = module_along(s, bl, dx, m);
        Pt tux = {dx.x * b_or(b, mx), dx.y * b_or(b, mx)}, tuy = {dy.x * b_or(e, my), dy.y * b_or(e, my)};
        Pt bux = {dx.x * b_or(f, mx), dx.y * b_or(f, mx)}, buy = {dy.x * b_or(d, my), dy.y * b_or(d, my)};
        nv = size_from_version_info(s, tr, bl, tux, tuy, bux, buy);
    }
    if (nv) sizes[ns++] = nv;
    const int deltas[5] = {0, -4, 4, -8, 8};
    for (int k = 0; k < 5; k++)
        if (n0 + deltas[k] != nv) sizes[ns++] = n0 + deltas[k];
    DBG("    version info -> size %d\n", nv);
    int rc = SQQR_ERR_NOT_FOUND;
    for (int k = 0; k < ns; k++) {
        int n = sizes[k];
        if (n < 21 || n > SQQR_MAX_SIZE) continue;
        Model md;
        int used = fit_symbol(s, tl, tr, bl, n, &md);
        if (used < 0) continue;
        int r = sample_and_decode(s, &md, n, grid, out);
        DBG("    n %d: %d alignment patterns -> %d\n", n, used, r);
        if (r == SQQR_OK) return r;
        if (used == 0 && n <= 45) {
            /* No alignment pattern (version 1, or not found in a small symbol): the three finders
             * fix an affine map only, so perspective moves the bottom-right
             * corner. Try nearby corners; the format BCH and Reed-Solomon reject
             * wrong samplings. */
            double span = n - 7;
            Pt ux = {(tr.x - tl.x) / span, (tr.y - tl.y) / span}, uy = {(bl.x - tl.x) / span, (bl.y - tl.y) / span};
            Pt s4[4] = {{3.5, 3.5}, {n - 3.5, 3.5}, {3.5, n - 3.5}, {n - 3.5, n - 3.5}};
            for (int step = 1; step <= 6 && r != SQQR_OK; step++)
                for (int oy = -step; oy <= step && r != SQQR_OK; oy++)
                    for (int ox = -step; ox <= step && r != SQQR_OK; ox++) {
                        if (abs(ox) != step && abs(oy) != step) continue; /* ring by ring */
                        double fx = ox * 0.5, fy = oy * 0.5;
                        Pt d4[4] = {tl, tr, bl,
                                    {tr.x + bl.x - tl.x + ux.x * fx + uy.x * fy,
                                     tr.y + bl.y - tl.y + ux.y * fx + uy.y * fy}};
                        md.k = 0; /* plain homography */
                        if (fit_h(s4, d4, 4, md.hm)) r = sample_and_decode(s, &md, n, grid, out);
                    }
            if (r == SQQR_OK) return r;
        }
        if (k == 0) rc = r;
    }
    return rc;
}

/* ---- 5. driver --------------------------------------------------------------- */

int sqqr_decode_image(const uint8_t *gray, int w, int h, int stride, void *work, size_t work_size,
                      SqqrResult *out) {
    if (!gray || !work || !out || w < 21 || h < 21 || stride < w || work_size < SQQR_IMAGE_WORK(w, h))
        return SQQR_ERR_SIZE;
    uint8_t *grid = (uint8_t *)work;
    uint8_t *bin = grid + (size_t)SQQR_MAX_SIZE * SQQR_MAX_SIZE;
    uint8_t *stats = bin + (size_t)w * (size_t)h;
    binarize(gray, w, h, stride, bin, stats);
    Scan s;
    s.bin = bin;
    s.w = w;
    s.h = h;
    int rc = SQQR_ERR_NOT_FOUND;
    for (int pass = 0; pass < 2; pass++) {
        if (pass) /* light-on-dark symbols */
            for (size_t i = 0; i < (size_t)w * (size_t)h; i++) bin[i] ^= 1;
        find_finders(&s);
        Triple t[MAX_TRIPLES];
        int nt = pick_triples(&s, t);
        DBG(" pass %d: %d finders, %d triples\n", pass, s.nf, nt);
        for (int i = 0; i < nt; i++) {
            int r = try_triple(&s, &t[i], grid, out);
            if (r == SQQR_OK) return r;
            if (rc == SQQR_ERR_NOT_FOUND) rc = r;
        }
    }
    return rc;
}

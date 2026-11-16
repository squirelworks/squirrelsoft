/*
 * Small dense linear solve (Gauss elimination with partial pivoting).
 *
 * NR 3rd ed. Ch. 2.1–2.3 (Gaussian elimination, pivoting) — ideas only.
 * Original Soft implementation; n <= SQN_MAX_DIM.
 */
#include "squirrelnum.h"

#include <math.h>
#include <string.h>

int sqn_gesv(double *A, double *b, int n, double *x) {
    if (!A || !b || !x || n <= 0 || n > SQN_MAX_DIM) return SQN_ERR_ARGS;
    double M[SQN_MAX_DIM * SQN_MAX_DIM];
    double rhs[SQN_MAX_DIM];
    memcpy(M, A, (size_t)(n * n) * sizeof(double));
    memcpy(rhs, b, (size_t)n * sizeof(double));

    int i, j, k;
    for (k = 0; k < n; k++) {
        int piv = k;
        double best = fabs(M[k * n + k]);
        for (i = k + 1; i < n; i++) {
            double v = fabs(M[i * n + k]);
            if (v > best) {
                best = v;
                piv = i;
            }
        }
        if (best < 1e-15) return SQN_ERR_SINGULAR;
        if (piv != k) {
            for (j = 0; j < n; j++) {
                double tmp = M[k * n + j];
                M[k * n + j] = M[piv * n + j];
                M[piv * n + j] = tmp;
            }
            double tb = rhs[k];
            rhs[k] = rhs[piv];
            rhs[piv] = tb;
        }
        double akk = M[k * n + k];
        for (i = k + 1; i < n; i++) {
            double f = M[i * n + k] / akk;
            M[i * n + k] = 0.0;
            for (j = k + 1; j < n; j++) M[i * n + j] -= f * M[k * n + j];
            rhs[i] -= f * rhs[k];
        }
    }
    for (i = n - 1; i >= 0; i--) {
        double s = rhs[i];
        for (j = i + 1; j < n; j++) s -= M[i * n + j] * x[j];
        x[i] = s / M[i * n + i];
    }
    return SQN_OK;
}

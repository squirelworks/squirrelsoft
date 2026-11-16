/*
 * Analytic verification suite (Soft #172 / Fred D1).
 * Separates verification (code solves its equations) from validation
 * (equations match the aircraft). All reports are provisional.
 */
#ifndef FL_VERIFY_H
#define FL_VERIFY_H

#include "fl_dyn.h"

#define FL_VERIFY_PROVISIONAL \
    "provisional, unvalidated against tunnel/flight test"

typedef struct {
    char name[48];
    int passed;
    double measured;
    double expected;
    double tol;
    char detail[192];
} FlVerifyCase;

typedef struct {
    FlVerifyCase cases[16];
    int ncases;
    int npass;
    int nfail;
    char label[80]; /* always provisional */
} FlVerifyReport;

/* Run built-in analytic cases; fill report. Returns 0 if all pass. */
int fl_verify_run_analytic(FlVerifyReport *rep);

/* Optional: ingest OpenVSP/AVL derivative summary files if present.
 * Returns 0 if compared, 1 if skipped (missing), -1 on hard error. */
int fl_verify_optional_xcheck(const char *openvsp_path, const char *avl_path,
                              FlVerifyReport *rep, char *err, int err_cap);

/* Markdown report into out. */
int fl_verify_report_md(const FlVerifyReport *rep, char *out, int out_cap);

#endif

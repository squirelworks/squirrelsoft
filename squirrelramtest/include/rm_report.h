#ifndef RM_REPORT_H
#define RM_REPORT_H

#include "rm_iface.h"
#include "rm_selftest.h"

/* Write a Soft pass/fail artifact (text). Returns 0 on success. */
int rm_report_write(const char *path, const RmIface *m, const RmTestResult *res);

#endif

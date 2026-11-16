#ifndef HT_EXPORT_H
#define HT_EXPORT_H

#include "ht_page.h"

/* Write page as JSON (mission-computer / Flight Lab consumable) into buf. */
int ht_export_json(const HtPage *p, char *buf, int cap);
/* Write page as SQHUD text format into buf. */
int ht_export_text(const HtPage *p, char *buf, int cap);
/* Write buf to path; returns 0 on success. */
int ht_write_file(const char *path, const char *buf);

#endif

/*
 * ICD message catalog fixture loader.
 * Protocol SoT is ISM fighter-jet/interfaces/avionics-bus.yaml (+ sms-interface.yaml).
 * Soft holds sample catalog fixtures only.
 */
#ifndef BM_CATALOG_H
#define BM_CATALOG_H

#include "bm_util.h"

typedef struct {
    char name[BM_NAME_LEN];
    char bus[BM_NAME_LEN];
    unsigned msgid;
    int rate_hz;
    char payload[BM_NAME_LEN];
    int opaque; /* 1 = SMS/stores: topic + safe display fields only */
} BmTopic;

typedef struct {
    char aircraft[BM_NAME_LEN];
    char revision[16];
    char source[BM_LINE_LEN];
    BmTopic topics[BM_MAX_TOPICS];
    int ntopics;
} BmCatalog;

int bm_catalog_parse(BmCatalog *cat, const char *text, char *err, int err_cap);
const BmTopic *bm_catalog_find_topic(const BmCatalog *cat, const char *name);
const BmTopic *bm_catalog_find_msgid(const BmCatalog *cat, unsigned msgid);

#endif

/* Message catalog: topic names from ISM ICD drafts. Original Soft code. */
#ifndef MC_CATALOG_H
#define MC_CATALOG_H

#include <stddef.h>

#define MC_TOPIC_MAX 64
#define MC_NAME_LEN 48
#define MC_MSG_LEN 160

typedef struct {
    char name[MC_NAME_LEN];
} McTopic;

typedef struct {
    McTopic topics[MC_TOPIC_MAX];
    int ntopics;
    char aircraft_id[MC_NAME_LEN];
} McCatalog;

/* Parse a minimal YAML fixture: lines "- name: topic.id". */
int mc_catalog_parse(McCatalog *out, const char *text, char *err, size_t errlen);

/* Look up a topic by exact name. Returns index or -1. */
int mc_catalog_find(const McCatalog *cat, const char *name);

/* Required topics the stub roles publish or display. */
int mc_catalog_has_required(const McCatalog *cat, char *err, size_t errlen);

#endif

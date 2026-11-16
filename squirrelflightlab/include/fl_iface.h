/*
 * Load ISM fighter-jet/interfaces YAML by Soft-owned schema allowlists.
 * SoT remains ISM; Soft only validates schema_version + required/allowed keys.
 */
#ifndef FL_IFACE_H
#define FL_IFACE_H

#include "fl_yaml.h"

#include <stddef.h>

#define FL_IFACE_MAX_FILES 32
#define FL_IFACE_MAX_KEYS 48
#define FL_IFACE_PATH_LEN 256
#define FL_IFACE_MSG_LEN 512

typedef struct {
    char name[96];
    char required[FL_IFACE_MAX_KEYS][FL_YAML_KEY_LEN];
    int nrequired;
    char allowed[FL_IFACE_MAX_KEYS][FL_YAML_KEY_LEN];
    int nallowed;
} FlIfaceFileSchema;

typedef struct {
    char schema_version[32];
    char supported[8][32];
    int nsupported;
    FlIfaceFileSchema files[FL_IFACE_MAX_FILES];
    int nfiles;
} FlIfaceSchema;

typedef struct {
    char path[FL_IFACE_PATH_LEN]; /* basename */
    char schema_version[32];
    char aircraft_id[64];
    int ok;
} FlIfaceDoc;

typedef struct {
    FlIfaceDoc docs[FL_IFACE_MAX_FILES];
    int ndocs;
    char aircraft_id[64];
    char schema_version[32];
    int nerrors;
    char errors[24][FL_IFACE_MSG_LEN];
} FlIfaceBundle;

/* Parse Soft allowlist schema (schemas/ism-fj-1.0.0/top-level-allowlist.yaml). */
int fl_iface_schema_load(FlIfaceSchema *out, const char *text, char *err,
                         int err_cap);

/*
 * Validate one interface YAML document against Soft schema for basename.
 * If doc_out is non-NULL, fills metadata. If tree_out is non-NULL, copies the
 * parsed document there (caller owns storage).
 */
int fl_iface_validate_doc(const FlIfaceSchema *schema, const char *basename,
                          const char *text, FlIfaceDoc *doc_out, FlYDoc *tree_out,
                          char *err, int err_cap);

/*
 * Load every YAML file under dir listed by schema-manifest.yaml (and Soft
 * allowlist). Fail loudly on missing/unknown top-level keys, unsupported
 * schema_version, missing manifest entries, or extra YAML not in Soft schema.
 */
int fl_iface_load_dir(FlIfaceBundle *bundle, const char *dir,
                      const char *soft_schema_path, char *err, int err_cap);

int fl_iface_report(const FlIfaceBundle *bundle, char *out, int out_cap);

#endif

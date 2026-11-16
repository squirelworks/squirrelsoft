#define _POSIX_C_SOURCE 200809L
#include "fl_iface.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, int err_cap, const char *msg) {
    if (err && err_cap > 0) snprintf(err, err_cap, "%s", msg);
}

static int add_error(FlIfaceBundle *b, const char *msg) {
    if (!b) return -1;
    if (b->nerrors < (int)(sizeof b->errors / sizeof b->errors[0])) {
        snprintf(b->errors[b->nerrors], sizeof b->errors[0], "%s", msg);
        b->nerrors++;
    }
    return -1;
}

static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || n > 4 << 20) {
        fclose(f);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return -1;
    }
    buf[n] = 0;
    fclose(f);
    *out = buf;
    return 0;
}

static int parse_str_list(FlYNode *node, char (*arr)[FL_YAML_KEY_LEN], int max,
                          int *nout) {
    *nout = 0;
    if (!node) return 0;
    if (node->kind == FL_Y_SEQ) {
        for (FlYNode *c = node->child; c && *nout < max; c = c->next) {
            snprintf(arr[*nout], FL_YAML_KEY_LEN, "%s", fl_yaml_as_cstr(c));
            (*nout)++;
        }
        return 0;
    }
    if (node->kind == FL_Y_STR || node->kind == FL_Y_NUM || node->kind == FL_Y_BOOL) {
        snprintf(arr[0], FL_YAML_KEY_LEN, "%s", fl_yaml_as_cstr(node));
        *nout = 1;
        return 0;
    }
    return -1;
}

/* supported[] uses shorter rows than FL_YAML_KEY_LEN — copy via temp. */
static int parse_str_list_short(FlYNode *node, char (*arr)[32], int max, int *nout) {
    char tmp[FL_IFACE_MAX_KEYS][FL_YAML_KEY_LEN];
    int n = 0;
    if (parse_str_list(node, tmp, max, &n) != 0) return -1;
    *nout = n;
    for (int i = 0; i < n; i++) snprintf(arr[i], 32, "%s", tmp[i]);
    return 0;
}

int fl_iface_schema_load(FlIfaceSchema *out, const char *text, char *err,
                         int err_cap) {
    if (!out || !text) {
        set_err(err, err_cap, "iface schema: null args");
        return -1;
    }
    memset(out, 0, sizeof *out);
    FlYDoc *doc = calloc(1, sizeof *doc);
    if (!doc) {
        set_err(err, err_cap, "iface schema: oom");
        return -1;
    }
    if (fl_yaml_parse(doc, text, err, err_cap) != 0) {
        free(doc);
        return -1;
    }
    FlYNode *root = doc->root;
    FlYNode *sv = fl_yaml_map_get(root, "schema_version");
    if (!sv) {
        set_err(err, err_cap, "iface schema: missing schema_version");
        free(doc);
        return -1;
    }
    snprintf(out->schema_version, sizeof out->schema_version, "%s",
             fl_yaml_as_cstr(sv));

    FlYNode *sup = fl_yaml_map_get(root, "supported_schema_versions");
    if (sup &&
        parse_str_list_short(sup, out->supported, 8, &out->nsupported) != 0) {
        set_err(err, err_cap, "iface schema: bad supported_schema_versions");
        free(doc);
        return -1;
    }
    if (out->nsupported == 0) {
        snprintf(out->supported[0], sizeof out->supported[0], "%s",
                 out->schema_version);
        out->nsupported = 1;
    }

    FlYNode *ifaces = fl_yaml_map_get(root, "interfaces");
    if (!ifaces || ifaces->kind != FL_Y_MAP) {
        set_err(err, err_cap, "iface schema: missing interfaces map");
        free(doc);
        return -1;
    }
    for (FlYNode *f = ifaces->child; f; f = f->next) {
        if (out->nfiles >= FL_IFACE_MAX_FILES) {
            set_err(err, err_cap, "iface schema: too many files");
            free(doc);
            return -1;
        }
        FlIfaceFileSchema *fs = &out->files[out->nfiles++];
        snprintf(fs->name, sizeof fs->name, "%s", f->key);
        if (f->kind != FL_Y_MAP) {
            snprintf(err, err_cap, "iface schema: %s not a map", f->key);
            free(doc);
            return -1;
        }
        FlYNode *req = fl_yaml_map_get(f, "required");
        FlYNode *all = fl_yaml_map_get(f, "allowed");
        if (parse_str_list(req, fs->required, FL_IFACE_MAX_KEYS, &fs->nrequired) !=
            0) {
            snprintf(err, err_cap, "iface schema: %s.required bad", f->key);
            free(doc);
            return -1;
        }
        if (parse_str_list(all, fs->allowed, FL_IFACE_MAX_KEYS, &fs->nallowed) !=
            0) {
            snprintf(err, err_cap, "iface schema: %s.allowed bad", f->key);
            free(doc);
            return -1;
        }
        FlYNode *nested = fl_yaml_map_get(f, "nested");
        if (nested) {
            if (nested->kind != FL_Y_MAP) {
                snprintf(err, err_cap, "iface schema: %s.nested not a map", f->key);
                free(doc);
                return -1;
            }
            for (FlYNode *nr = nested->child; nr; nr = nr->next) {
                if (fs->nnested >= FL_IFACE_MAX_NESTED) {
                    snprintf(err, err_cap, "iface schema: %s too many nested rules",
                             f->key);
                    free(doc);
                    return -1;
                }
                FlIfaceNestedRule *rule = &fs->nested[fs->nnested++];
                snprintf(rule->path, sizeof rule->path, "%s", nr->key);
                if (nr->kind != FL_Y_MAP) {
                    snprintf(err, err_cap, "iface schema: %s.nested.%s not a map",
                             f->key, nr->key);
                    free(doc);
                    return -1;
                }
                if (parse_str_list(fl_yaml_map_get(nr, "required"), rule->required,
                                   FL_IFACE_NEST_KEYS, &rule->nrequired) != 0) {
                    snprintf(err, err_cap, "iface schema: %s.nested.%s.required bad",
                             f->key, nr->key);
                    free(doc);
                    return -1;
                }
                if (parse_str_list(fl_yaml_map_get(nr, "allowed"), rule->allowed,
                                   FL_IFACE_NEST_KEYS, &rule->nallowed) != 0) {
                    snprintf(err, err_cap, "iface schema: %s.nested.%s.allowed bad",
                             f->key, nr->key);
                    free(doc);
                    return -1;
                }
            }
        }
    }
    free(doc);
    return 0;
}

static const FlIfaceFileSchema *find_file_schema(const FlIfaceSchema *schema,
                                                 const char *basename) {
    for (int i = 0; i < schema->nfiles; i++)
        if (strcmp(schema->files[i].name, basename) == 0) return &schema->files[i];
    return NULL;
}

static int version_supported(const FlIfaceSchema *schema, const char *ver) {
    for (int i = 0; i < schema->nsupported; i++)
        if (strcmp(schema->supported[i], ver) == 0) return 1;
    return 0;
}

static int key_in_list(const char (*list)[FL_YAML_KEY_LEN], int n, const char *k) {
    for (int i = 0; i < n; i++)
        if (strcmp(list[i], k) == 0) return 1;
    return 0;
}

/* Validate one map against a nested required/allowed closed list. */
static int validate_closed_map(const FlYNode *map, const FlIfaceNestedRule *rule,
                               const char *basename, const char *path_ctx, char *err,
                               int err_cap) {
    if (!map || map->kind != FL_Y_MAP) {
        snprintf(err, err_cap, "%s: %s is not a map", basename, path_ctx);
        return -1;
    }
    for (int i = 0; i < rule->nrequired; i++) {
        if (!fl_yaml_map_get(map, rule->required[i])) {
            snprintf(err, err_cap, "%s: missing required key '%s.%s'", basename,
                     path_ctx, rule->required[i]);
            return -1;
        }
    }
    for (const FlYNode *c = map->child; c; c = c->next) {
        if (!key_in_list(rule->allowed, rule->nallowed, c->key)) {
            snprintf(err, err_cap, "%s: unknown key '%s.%s'", basename, path_ctx,
                     c->key);
            return -1;
        }
    }
    return 0;
}

/*
 * Resolve Soft nested path (dotted segments; trailing [] means every seq
 * element). Soft #177. Examples: envelope  surfaces[]  full_scale.cg_empty
 */
static int apply_nested_rule(const FlYNode *root, const FlIfaceNestedRule *rule,
                             const char *basename, char *err, int err_cap) {
    char path_copy[FL_IFACE_NEST_PATH];
    snprintf(path_copy, sizeof path_copy, "%s", rule->path);
    if (!path_copy[0]) {
        snprintf(err, err_cap, "%s: empty nested path", basename);
        return -1;
    }

    /* Split into segments once (max depth small). */
    char *segs[16];
    int nsegs = 0;
    for (char *p = path_copy; *p && nsegs < 16;) {
        segs[nsegs++] = p;
        char *dot = strchr(p, '.');
        if (!dot) break;
        *dot = 0;
        p = dot + 1;
    }

    const FlYNode *cur = root;
    char ctx[FL_IFACE_NEST_PATH];
    ctx[0] = 0;

    for (int si = 0; si < nsegs; si++) {
        char *seg = segs[si];
        int is_seq = 0;
        size_t seglen = strlen(seg);
        if (seglen >= 2 && seg[seglen - 2] == '[' && seg[seglen - 1] == ']') {
            is_seq = 1;
            seg[seglen - 2] = 0;
        }
        if (!cur || cur->kind != FL_Y_MAP) {
            snprintf(err, err_cap, "%s: nested path '%s' parent is not a map",
                     basename, rule->path);
            return -1;
        }
        const FlYNode *next = fl_yaml_map_get(cur, seg);
        if (!next) {
            snprintf(err, err_cap, "%s: missing required nested path '%s'",
                     basename, rule->path);
            return -1;
        }

        char next_ctx[FL_IFACE_NEST_PATH];
        if (ctx[0])
            snprintf(next_ctx, sizeof next_ctx, "%s.%s", ctx, seg);
        else
            snprintf(next_ctx, sizeof next_ctx, "%s", seg);

        int last = (si == nsegs - 1);

        if (is_seq) {
            if (next->kind != FL_Y_SEQ) {
                snprintf(err, err_cap, "%s: %s is not a sequence", basename,
                         next_ctx);
                return -1;
            }
            if (!last) {
                /* Remaining path applies under each sequence element map. */
                FlIfaceNestedRule sub = *rule;
                char rest[FL_IFACE_NEST_PATH];
                rest[0] = 0;
                for (int j = si + 1; j < nsegs; j++) {
                    size_t used = strlen(rest);
                    snprintf(rest + used, sizeof rest - used, "%s%s",
                             used ? "." : "", segs[j]);
                }
                snprintf(sub.path, sizeof sub.path, "%s", rest);
                int idx = 0;
                for (const FlYNode *it = next->child; it; it = it->next, idx++) {
                    if (it->kind != FL_Y_MAP) {
                        snprintf(err, err_cap, "%s: %s[%d] is not a map", basename,
                                 next_ctx, idx);
                        return -1;
                    }
                    if (apply_nested_rule(it, &sub, basename, err, err_cap) != 0)
                        return -1;
                }
                return 0;
            }
            int idx = 0;
            for (const FlYNode *it = next->child; it; it = it->next, idx++) {
                char item_ctx[FL_IFACE_NEST_PATH];
                snprintf(item_ctx, sizeof item_ctx, "%s[%d]", next_ctx, idx);
                if (validate_closed_map(it, rule, basename, item_ctx, err,
                                        err_cap) != 0)
                    return -1;
            }
            return 0;
        }

        snprintf(ctx, sizeof ctx, "%s", next_ctx);
        cur = next;
        if (last)
            return validate_closed_map(cur, rule, basename, ctx, err, err_cap);
    }
    snprintf(err, err_cap, "%s: empty nested path", basename);
    return -1;
}

int fl_iface_validate_doc(const FlIfaceSchema *schema, const char *basename,
                          const char *text, FlIfaceDoc *doc_out, FlYDoc *tree_out,
                          char *err, int err_cap) {
    if (!schema || !basename || !text) {
        set_err(err, err_cap, "iface validate: null args");
        return -1;
    }
    if (doc_out) memset(doc_out, 0, sizeof *doc_out);
    if (doc_out) snprintf(doc_out->path, sizeof doc_out->path, "%s", basename);

    const FlIfaceFileSchema *fs = find_file_schema(schema, basename);
    if (!fs) {
        snprintf(err, err_cap, "%s: unknown interface file (not in Soft schema)",
                 basename);
        return -1;
    }

    FlYDoc *heap = NULL;
    FlYDoc *doc = tree_out;
    if (!doc) {
        heap = calloc(1, sizeof *heap);
        if (!heap) {
            set_err(err, err_cap, "iface validate: oom");
            return -1;
        }
        doc = heap;
    } else {
        memset(doc, 0, sizeof *doc);
    }

    if (fl_yaml_parse(doc, text, err, err_cap) != 0) {
        free(heap);
        return -1;
    }
    FlYNode *root = doc->root;
    if (!root || root->kind != FL_Y_MAP) {
        snprintf(err, err_cap, "%s: root is not a map", basename);
        free(heap);
        return -1;
    }

    FlYNode *sv = fl_yaml_map_get(root, "schema_version");
    if (!sv) {
        snprintf(err, err_cap, "%s: missing required key 'schema_version'",
                 basename);
        free(heap);
        return -1;
    }
    const char *ver = fl_yaml_as_cstr(sv);
    if (doc_out)
        snprintf(doc_out->schema_version, sizeof doc_out->schema_version, "%s",
                 ver);
    if (!version_supported(schema, ver)) {
        snprintf(err, err_cap,
                 "%s: unsupported schema_version '%s' (Soft supports Soft "
                 "schema %s)",
                 basename, ver, schema->schema_version);
        free(heap);
        return -1;
    }

    FlYNode *aid = fl_yaml_map_get(root, "aircraft_id");
    if (aid && doc_out)
        snprintf(doc_out->aircraft_id, sizeof doc_out->aircraft_id, "%s",
                 fl_yaml_as_cstr(aid));

    for (int i = 0; i < fs->nrequired; i++) {
        if (!fl_yaml_map_get(root, fs->required[i])) {
            snprintf(err, err_cap, "%s: missing required key '%s'", basename,
                     fs->required[i]);
            free(heap);
            return -1;
        }
    }

    for (FlYNode *c = root->child; c; c = c->next) {
        if (!key_in_list(fs->allowed, fs->nallowed, c->key)) {
            snprintf(err, err_cap, "%s: unknown key '%s'", basename, c->key);
            free(heap);
            return -1;
        }
    }

    for (int i = 0; i < fs->nnested; i++) {
        if (apply_nested_rule(root, &fs->nested[i], basename, err, err_cap) != 0) {
            free(heap);
            return -1;
        }
    }

    if (doc_out) doc_out->ok = 1;
    free(heap);
    return 0;
}

static int ends_with_yaml(const char *name) {
    size_t n = strlen(name);
    return n > 5 && strcmp(name + n - 5, ".yaml") == 0;
}

int fl_iface_load_dir(FlIfaceBundle *bundle, const char *dir,
                      const char *soft_schema_path, char *err, int err_cap) {
    if (!bundle || !dir || !soft_schema_path) {
        set_err(err, err_cap, "iface load_dir: null args");
        return -1;
    }
    memset(bundle, 0, sizeof *bundle);

    char *schema_text = NULL;
    if (read_file(soft_schema_path, &schema_text) != 0) {
        snprintf(err, err_cap, "cannot read Soft schema %s", soft_schema_path);
        return -1;
    }
    FlIfaceSchema *schema = calloc(1, sizeof *schema);
    if (!schema) {
        free(schema_text);
        set_err(err, err_cap, "oom");
        return -1;
    }
    if (fl_iface_schema_load(schema, schema_text, err, err_cap) != 0) {
        free(schema_text);
        free(schema);
        return -1;
    }
    free(schema_text);

    char manifest_path[FL_IFACE_PATH_LEN + 32];
    snprintf(manifest_path, sizeof manifest_path, "%s/schema-manifest.yaml", dir);
    char *manifest_text = NULL;
    if (read_file(manifest_path, &manifest_text) != 0) {
        snprintf(err, err_cap, "missing schema-manifest.yaml in %s", dir);
        return -1;
    }

    FlYDoc *manifest_tree = calloc(1, sizeof *manifest_tree);
    if (!manifest_tree) {
        free(manifest_text);
        set_err(err, err_cap, "oom");
        return -1;
    }

    FlIfaceDoc manifest_doc;
    char local_err[FL_IFACE_MSG_LEN];
    if (fl_iface_validate_doc(schema, "schema-manifest.yaml", manifest_text,
                              &manifest_doc, manifest_tree, local_err,
                              sizeof local_err) != 0) {
        add_error(bundle, local_err);
        snprintf(err, err_cap, "%s", local_err);
        free(manifest_text);
        free(manifest_tree);
        free(schema);
        return -1;
    }
    free(manifest_text);

    char listed[FL_IFACE_MAX_FILES][96];
    int nlisted = 0;
    FlYNode *files = fl_yaml_map_get(manifest_tree->root, "files");
    if (!files || files->kind != FL_Y_SEQ) {
        set_err(err, err_cap, "schema-manifest.yaml: files must be a sequence");
        free(manifest_tree);
        free(schema);
        return -1;
    }
    for (FlYNode *it = files->child; it; it = it->next) {
        const char *path = NULL;
        if (it->kind == FL_Y_MAP) {
            FlYNode *p = fl_yaml_map_get(it, "path");
            if (p) path = fl_yaml_as_cstr(p);
        } else {
            path = fl_yaml_as_cstr(it);
        }
        if (!path || !*path) {
            set_err(err, err_cap, "schema-manifest.yaml: files entry missing path");
            free(manifest_tree);
            free(schema);
            return -1;
        }
        if (nlisted >= FL_IFACE_MAX_FILES) {
            set_err(err, err_cap, "schema-manifest.yaml: too many files");
            free(manifest_tree);
            free(schema);
            return -1;
        }
        snprintf(listed[nlisted], sizeof listed[0], "%s", path);
        nlisted++;
    }
    free(manifest_tree);

    snprintf(bundle->aircraft_id, sizeof bundle->aircraft_id, "%s",
             manifest_doc.aircraft_id);
    snprintf(bundle->schema_version, sizeof bundle->schema_version, "%s",
             manifest_doc.schema_version);

    if (bundle->ndocs >= FL_IFACE_MAX_FILES) {
        set_err(err, err_cap, "too many interface docs");
        free(schema);
        return -1;
    }
    bundle->docs[bundle->ndocs++] = manifest_doc;

    for (int i = 0; i < nlisted; i++) {
        char path[FL_IFACE_PATH_LEN + 128];
        snprintf(path, sizeof path, "%s/%s", dir, listed[i]);
        char *text = NULL;
        if (read_file(path, &text) != 0) {
            snprintf(local_err, sizeof local_err,
                     "%s: file missing (listed in manifest)", listed[i]);
            add_error(bundle, local_err);
            continue;
        }
        if (bundle->ndocs >= FL_IFACE_MAX_FILES) {
            free(text);
            set_err(err, err_cap, "too many interface docs");
            free(schema);
            return -1;
        }
        FlIfaceDoc *d = &bundle->docs[bundle->ndocs];
        if (fl_iface_validate_doc(schema, listed[i], text, d, NULL, local_err,
                                  sizeof local_err) != 0) {
            add_error(bundle, local_err);
            free(text);
            continue;
        }
        free(text);
        if (d->aircraft_id[0] && bundle->aircraft_id[0] &&
            strcmp(d->aircraft_id, bundle->aircraft_id) != 0) {
            snprintf(local_err, sizeof local_err,
                     "%s: aircraft_id '%s' != manifest '%s'", listed[i],
                     d->aircraft_id, bundle->aircraft_id);
            d->ok = 0;
            add_error(bundle, local_err);
            continue;
        }
        bundle->ndocs++;
    }

    DIR *dp = opendir(dir);
    if (!dp) {
        snprintf(err, err_cap, "cannot open directory %s", dir);
        free(schema);
        return -1;
    }
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!ends_with_yaml(de->d_name)) continue;
        if (strcmp(de->d_name, "schema-manifest.yaml") == 0) continue;
        int listed_ok = 0;
        for (int i = 0; i < nlisted; i++)
            if (strcmp(listed[i], de->d_name) == 0) {
                listed_ok = 1;
                break;
            }
        if (!listed_ok) {
            snprintf(local_err, sizeof local_err,
                     "%s: present in directory but not listed in "
                     "schema-manifest.yaml",
                     de->d_name);
            add_error(bundle, local_err);
            continue;
        }
        if (!find_file_schema(schema, de->d_name)) {
            snprintf(local_err, sizeof local_err,
                     "%s: listed/present but missing from Soft allowlist schema",
                     de->d_name);
            add_error(bundle, local_err);
        }
    }
    closedir(dp);

    for (int i = 0; i < nlisted; i++) {
        if (!find_file_schema(schema, listed[i])) {
            snprintf(local_err, sizeof local_err,
                     "%s: in ISM manifest but Soft allowlist has no entry "
                     "(refresh schemas/ism-fj-*)",
                     listed[i]);
            add_error(bundle, local_err);
        }
    }

    free(schema);
    if (bundle->nerrors) {
        snprintf(err, err_cap, "ism-interfaces: %d error(s); first: %s",
                 bundle->nerrors, bundle->errors[0]);
        return -1;
    }
    return 0;
}

int fl_iface_report(const FlIfaceBundle *bundle, char *out, int out_cap) {
    if (!bundle || !out || out_cap < 8) return -1;
    int w = 0;
    w += snprintf(out + w, out_cap - w,
                  "ism-interfaces: aircraft_id=%s schema_version=%s files=%d %s\n",
                  bundle->aircraft_id[0] ? bundle->aircraft_id : "?",
                  bundle->schema_version[0] ? bundle->schema_version : "?",
                  bundle->ndocs, bundle->nerrors ? "FAIL" : "PASS");
    for (int i = 0; i < bundle->ndocs && w < out_cap - 2; i++) {
        w += snprintf(out + w, out_cap - w, "  %s  schema_version=%s %s\n",
                      bundle->docs[i].path, bundle->docs[i].schema_version,
                      bundle->docs[i].ok ? "ok" : "bad");
    }
    for (int i = 0; i < bundle->nerrors && w < out_cap - 2; i++) {
        w += snprintf(out + w, out_cap - w, "  ERROR: %s\n", bundle->errors[i]);
    }
    return w < out_cap ? 0 : -1;
}

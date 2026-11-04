/* sqjson: a small JSON reader for card dumps (RFC 8259 values, UTF-8 text).
 * Original code. The whole document is parsed into one malloc'd tree. */
#ifndef SQJSON_H
#define SQJSON_H

#include <stddef.h>

typedef enum { SQJ_NULL, SQJ_FALSE, SQJ_TRUE, SQJ_NUMBER, SQJ_STRING, SQJ_ARRAY, SQJ_OBJECT } SqjType;

typedef struct SqjValue SqjValue;
struct SqjValue {
    SqjType type;
    double number;
    char *string;      /* SQJ_STRING: NUL-terminated UTF-8 */
    size_t len;        /* string length, or the number of children */
    SqjValue *child;   /* first element or member */
    SqjValue *next;    /* next sibling */
    char *key;         /* member key when inside an object */
};

/* Parse text[0..len). Returns the root or NULL with a message in err. */
SqjValue *sqj_parse(const char *text, size_t len, char *err, size_t errlen);
void sqj_free(SqjValue *v);

/* Object member by key, or NULL. */
const SqjValue *sqj_get(const SqjValue *obj, const char *key);
/* String member, or NULL if absent or not a string. */
const char *sqj_str(const SqjValue *obj, const char *key);

#endif

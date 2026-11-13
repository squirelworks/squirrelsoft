/*
 * sqqr_content: what a decoded QR payload means (original code). Reads the
 * common text conventions found in QR codes: Wi-Fi network (WIFI:),
 * contacts (vCard, MECARD), e-mail (mailto:, MATMSG:), phone (tel:), SMS
 * (SMSTO:, sms:), location (geo:) and web links; anything else is text.
 * Also builds Wi-Fi payloads for generating codes. C11, no allocation.
 */
#ifndef SQQR_CONTENT_H
#define SQQR_CONTENT_H

#include <stddef.h>

typedef enum {
    SQQR_CT_TEXT = 0,
    SQQR_CT_URL,
    SQQR_CT_WIFI,
    SQQR_CT_CONTACT,
    SQQR_CT_EMAIL,
    SQQR_CT_PHONE,
    SQQR_CT_SMS,
    SQQR_CT_GEO
} SqqrContentType;

#define SQQR_CONTENT_MAX_FIELDS 32

typedef struct {
    const char *label; /* "Network", "Password", "Name", "Phone", ... (static strings) */
    const char *value; /* UTF-8, points into buf */
} SqqrField;

typedef struct {
    SqqrContentType type;
    int nfields;
    SqqrField field[SQQR_CONTENT_MAX_FIELDS];
    char buf[8192]; /* the field values; long values are cut at a UTF-8 boundary */
} SqqrContent;

/* Parse NUL-terminated UTF-8 text. Always succeeds: unknown or malformed
 * structured text comes back as SQQR_CT_TEXT with no fields. */
void sqqr_content_parse(const char *text, SqqrContent *out);

/* "Text", "Web link", "Wi-Fi network", "Contact", "E-mail", "Phone",
 * "SMS", "Location" */
const char *sqqr_content_type_name(SqqrContentType t);
/* "text", "url", "wifi", "contact", "email", "phone", "sms", "geo" */
const char *sqqr_content_type_id(SqqrContentType t);

/* The first value with this label, or NULL */
const char *sqqr_content_get(const SqqrContent *c, const char *label);

/* WIFI:T:<security>;S:<ssid>;P:<password>;[H:true;]; with \ ; , : " escaped.
 * security is "WPA", "WEP", "SAE" or "nopass" (NULL or "" means WPA when
 * there is a password, nopass otherwise). Returns the length written, like
 * snprintf (out is always terminated when cap > 0). */
size_t sqqr_content_wifi(const char *ssid, const char *password, const char *security, int hidden,
                         char *out, size_t cap);

#endif

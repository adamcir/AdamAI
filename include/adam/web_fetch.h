/* AdamAI - GPL-3.0-only */
#ifndef ADAM_WEB_FETCH_H
#define ADAM_WEB_FETCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

#define ADAM_WEB_MAX_BYTES (512u * 1024u)
#define ADAM_WEB_MAX_TEXT 12000u
#define ADAM_WEB_MAX_URL 2048u

typedef struct {
    char *text; /* extracted readable UTF-8-ish document text (caller frees) */
    char *final_url; /* validated final HTTPS URL, if server redirects */
    long status;
    bool truncated;
} AdamWebPage;

/* Validates a public HTTPS URL (HTTPS, port 443, no credentials).
 * Connected IPs are independently checked inside adam_web_fetch(). */
bool adam_web_validate_url(const char *url, char *error, size_t error_size);
/* True only for publicly routable addresses, IPv4 and IPv6. */
bool adam_web_public_ip(const char *ip);
/* Convert a (possibly malformed) HTML document to readable text. */
char *adam_web_extract_text(const char *source, size_t size, bool html, bool *truncated);
/* Cancellable HTTP GET. curl_global_init must have been called by the app. */
bool adam_web_fetch(const char *url, const atomic_bool *cancelled,
                    AdamWebPage *page, char *error, size_t error_size);
void adam_web_page_free(AdamWebPage *page);
#endif

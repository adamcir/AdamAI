/* AdamAI - GPL-3.0-only */
#ifndef ADAM_WEB_LINKS_H
#define ADAM_WEB_LINKS_H
#include <stddef.h>

/* Extract limited, safe HTTPS links from HTML so Adam 1 can browse from
 * a search page to its results or follow links on a documentation site.
 * Resolves relative URLs against the source page. Returns allocated text.
 */
char *adam_web_extract_links(const char *html, size_t size, const char *base_url);
#endif

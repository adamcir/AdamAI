/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/web_fetch.h"
#include "adam/web_links.h"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void errorf(char *dst, size_t size, const char *fmt, ...) {
    if (!dst || !size) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, size, fmt, ap);
    va_end(ap);
}

static bool prefix_case(const char *p, const char *prefix) {
    for (; *prefix; ++prefix, ++p)
        if (!*p || tolower((unsigned char)*p) != tolower((unsigned char)*prefix)) return false;
    return true;
}

bool adam_web_public_ip(const char *ip) {
    unsigned char addr[16];
    if (!ip) return false;
    if (inet_pton(AF_INET, ip, addr) == 1) {
        unsigned a = addr[0], b = addr[1], c = addr[2];
        /* Reject loopback, RFC1918, link-local, CGNAT, docs, multicast,
         * benchmarking, reserved and special-purpose address spaces. */
        if (a == 0 || a == 10 || a == 127 || a >= 224 ||
            (a == 100 && b >= 64 && b <= 127) ||
            (a == 169 && b == 254) ||
            (a == 172 && b >= 16 && b <= 31) ||
            (a == 192 && (b == 168 || (b == 0 && c == 0) ||
                          (b == 0 && c == 2) || (b == 88 && c == 99))) ||
            (a == 198 && (b == 18 || b == 19 || (b == 51 && c == 100))) ||
            (a == 203 && b == 0 && c == 113)) return false;
        return true;
    }
    if (inet_pton(AF_INET6, ip, addr) == 1) {
        /* IPv4-mapped addresses must be checked as IPv4, not as IPv6. */
        static const unsigned char mapped[12] = {0,0,0,0,0,0,0,0,0,0,255,255};
        if (memcmp(addr, mapped, 12) == 0) {
            char v4[INET_ADDRSTRLEN];
            return inet_ntop(AF_INET, addr + 12, v4, sizeof(v4)) && adam_web_public_ip(v4);
        }
        /* IPv6 global unicast only, exclude documentation and special ranges. */
        if ((addr[0] & 0xE0) != 0x20) return false;
        if (addr[0] == 0x20 && addr[1] == 0x01 && addr[2] == 0x0d && addr[3] == 0xb8)
            return false;
        return true;
    }
    return false;
}

bool adam_web_validate_url(const char *url, char *error, size_t error_size) {
    if (!url || strncmp(url, "https://", 8) != 0 ||
        !url[8] || strlen(url) > ADAM_WEB_MAX_URL) {
        errorf(error, error_size, "Use an HTTPS URL (max %u bytes)", ADAM_WEB_MAX_URL);
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) {
        if (*p <= 32 || *p == 127) {
            errorf(error, error_size, "Spaces and control characters are not allowed in URL");
            return false;
        }
    }
    CURLU *u = curl_url();
    if (!u) return false;
    char *host = NULL, *port = NULL, *user = NULL, *pass = NULL;
    bool ok = curl_url_set(u, CURLUPART_URL, url, 0) == CURLUE_OK &&
              curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
              host && *host &&
              curl_url_get(u, CURLUPART_USER, &user, 0) != CURLUE_OK &&
              curl_url_get(u, CURLUPART_PASSWORD, &pass, 0) != CURLUE_OK;
    if (ok && curl_url_get(u, CURLUPART_PORT, &port, 0) == CURLUE_OK)
        ok = port && strcmp(port, "443") == 0;
    /* Avoid internal hostnames even before DNS resolution. The pre-request
     * hook separately rejects non-public resolved IPs (including rebinding). */
    if (ok && (prefix_case(host, "localhost") || strchr(host, '@') ||
               (strlen(host) >= 6 && !strcmp(host + strlen(host) - 6, ".local")))) ok = false;
    /* Explicit IP host literals receive the same public address filter. */
    if (ok && (strchr(host, ':') || isdigit((unsigned char)host[0]))) {
        unsigned char bytes[16];
        if (inet_pton(AF_INET, host, bytes) == 1 ||
            inet_pton(AF_INET6, host, bytes) == 1)
            ok = adam_web_public_ip(host);
    }
    curl_free(host); curl_free(port); curl_free(user); curl_free(pass);
    curl_url_cleanup(u);
    if (!ok) errorf(error, error_size,
                    "URL blocked: public HTTPS hosts only, port 443, no credentials");
    return ok;
}

typedef struct { char *data; size_t len; bool oversized; bool unsupported; } Body;

static size_t receive_body(char *ptr, size_t size, size_t count, void *userdata) {
    Body *b = userdata;
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) return 0;
    size_t n = count * size;
    if (b->len > ADAM_WEB_MAX_BYTES - (n > ADAM_WEB_MAX_BYTES ? ADAM_WEB_MAX_BYTES : n) ||
        n > ADAM_WEB_MAX_BYTES) { b->oversized = true; return 0; }
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}
static bool content_type_allowed(const char *v) {
    while (*v == ' ' || *v == '\t') ++v;
    return prefix_case(v, "text/") || prefix_case(v, "application/json") ||
           prefix_case(v, "application/xml") || prefix_case(v, "application/xhtml+xml");
}
static size_t receive_header(char *ptr, size_t size, size_t count, void *userdata) {
    Body *b = userdata;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if (n >= 13 && prefix_case(ptr, "Content-Type:")) {
        if (!content_type_allowed(ptr + 13)) { b->unsupported = true; return 0; }
    }
    return n;
}
static int deny_private_ip(void *ctx, char *remote_ip, char *local_ip, int remote_port, int local_port) {
    (void)ctx; (void)local_ip; (void)local_port;
    return remote_port == 443 && adam_web_public_ip(remote_ip) ?
           CURL_PREREQFUNC_OK : CURL_PREREQFUNC_ABORT;
}
static int progress(void *userdata, curl_off_t dl_total, curl_off_t dl_now,
                    curl_off_t ul_total, curl_off_t ul_now) {
    (void)dl_total; (void)dl_now; (void)ul_total; (void)ul_now;
    const atomic_bool *cancelled = userdata;
    return cancelled && atomic_load(cancelled) ? 1 : 0;
}

static void push_codepoint(char *out, size_t cap, size_t *len, unsigned cp) {
    unsigned char b[4]; size_t n;
    if (cp <= 0x7F) { b[0] = (unsigned char)cp; n = 1; }
    else if (cp <= 0x7FF) {
        b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 63); n = 2;
    } else if (cp <= 0xFFFF && (cp < 0xD800 || cp > 0xDFFF)) {
        b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 63);
        b[2] = 0x80 | (cp & 63); n = 3;
    } else if (cp <= 0x10FFFF) {
        b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 63);
        b[2] = 0x80 | ((cp >> 6) & 63); b[3] = 0x80 | (cp & 63); n = 4;
    } else return;
    if (*len + n >= cap) return;
    for (size_t i = 0; i < n; ++i) out[(*len)++] = (char)b[i];
}

static bool tag_name_is(const char *start, size_t count, const char *target) {
    size_t n = strlen(target);
    if (count != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)start[i]) != (unsigned char)target[i]) return false;
    return true;
}

static bool paragraph_tag(const char *start, size_t n) {
    return tag_name_is(start,n,"p") || tag_name_is(start,n,"div") ||
           tag_name_is(start,n,"br") || tag_name_is(start,n,"li") ||
           tag_name_is(start,n,"tr") || tag_name_is(start,n,"h1") ||
           tag_name_is(start,n,"h2") || tag_name_is(start,n,"h3") ||
           tag_name_is(start,n,"article") || tag_name_is(start,n,"section");
}

char *adam_web_extract_text(const char *source, size_t size, bool html, bool *truncated) {
    if (truncated) *truncated = false;
    if (!source) return NULL;
    size_t cap = ADAM_WEB_MAX_TEXT + 8;
    char *out = calloc(cap, 1);
    if (!out) return NULL;
    size_t used = 0;
    int hidden = 0; /* 1 script, 2 style, 3 svg, 4 noscript */
    bool space = false;
    for (size_t i = 0; i < size && used < ADAM_WEB_MAX_TEXT; ) {
        unsigned char ch = (unsigned char)source[i];
        if (html && ch == '<') {
            size_t k = i + 1;
            while (k < size && k - i < 8192 && source[k] != '>') ++k;
            if (k == size || k - i >= 8192) { ++i; continue; }
            size_t p = i + 1;
            while (p < k && isspace((unsigned char)source[p])) ++p;
            bool closing = p < k && source[p] == '/';
            if (closing) ++p;
            size_t begin = p;
            while (p < k && (isalnum((unsigned char)source[p]) || source[p] == '-')) ++p;
            size_t n = p - begin;
            int type = tag_name_is(source+begin,n,"script") ? 1 :
                       tag_name_is(source+begin,n,"style") ? 2 :
                       tag_name_is(source+begin,n,"svg") ? 3 :
                       tag_name_is(source+begin,n,"noscript") ? 4 : 0;
            if (type) { if (!closing) hidden = type; else if (hidden == type) hidden = 0; }
            else if (!hidden && paragraph_tag(source + begin, n)) {
                if (used && out[used - 1] != '\n') { out[used++] = '\n'; space = false; }
            }
            i = k + 1;
            continue;
        }
        if (hidden) { ++i; continue; }
        if (html && ch == '&') {
            size_t k = i + 1;
            while (k < size && k - i < 14 && source[k] != ';' &&
                   !isspace((unsigned char)source[k]) && source[k] != '<') ++k;
            if (k < size && source[k] == ';') {
                size_t len = k - (i + 1);
                const char *p = source + i + 1;
                unsigned cp = 0;
                if (len == 3 && !memcmp(p,"amp",3)) cp = '&';
                else if (len == 2 && !memcmp(p,"lt",2)) cp = '<';
                else if (len == 2 && !memcmp(p,"gt",2)) cp = '>';
                else if (len == 4 && !memcmp(p,"quot",4)) cp = '"';
                else if (len == 4 && !memcmp(p,"apos",4)) cp = '\'';
                else if (len == 4 && !memcmp(p,"nbsp",4)) cp = ' ';
                else if (len > 1 && p[0] == '#') {
                    unsigned long val = 0;
                    int base = 10; size_t from = 1;
                    if (len > 2 && (p[1] == 'x' || p[1] == 'X')) { base = 16; from = 2; }
                    bool ok = true;
                    for (size_t x = from; x < len; ++x) {
                        int v = isdigit((unsigned char)p[x]) ? p[x]-'0' :
                                (base == 16 && p[x] >= 'a' && p[x] <= 'f') ? p[x]-'a'+10 :
                                (base == 16 && p[x] >= 'A' && p[x] <= 'F') ? p[x]-'A'+10 : -1;
                        if (v < 0 || v >= base || val > 0x10FFFFu/(unsigned)base)
                            { ok = false; break; }
                        val = val*(unsigned)base + (unsigned)v;
                    }
                    if (ok && val > 0 && val <= 0x10FFFF) cp = (unsigned)val;
                }
                if (cp) {
                    if (cp == ' ') space = used > 0;
                    else { if (space && used < ADAM_WEB_MAX_TEXT) out[used++]=' ';
                           space=false; push_codepoint(out,cap,&used,cp); }
                    i = k + 1;
                    continue;
                }
            }
        }
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
            space = used > 0 && out[used-1] != '\n'; ++i; continue;
        }
        if (ch < 32 || ch == 127) { ++i; continue; }
        if (space && used < ADAM_WEB_MAX_TEXT) out[used++] = ' ';
        space = false;
        if (used < ADAM_WEB_MAX_TEXT) out[used++] = (char)ch;
        ++i;
    }
    while (used && (out[used-1] == ' ' || out[used-1] == '\n')) --used;
    out[used] = '\0';
    if (truncated) *truncated = used >= ADAM_WEB_MAX_TEXT;
    return out;
}

bool adam_web_fetch(const char *url, const atomic_bool *cancelled,
                    AdamWebPage *page, char *error, size_t error_size) {
    if (!page) return false;
    memset(page, 0, sizeof(*page));
    if (!adam_web_validate_url(url, error, error_size)) return false;
    if (cancelled && atomic_load(cancelled)) {
        errorf(error,error_size,"Cancelled"); return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) { errorf(error,error_size,"Could not initialize libcurl"); return false; }
    Body b = {.data = malloc(ADAM_WEB_MAX_BYTES + 1)};
    if (!b.data) { curl_easy_cleanup(curl); return false; }
    b.data[0] = '\0';
    CURLcode settings = CURLE_OK;
#define SET(option,value) do { if (settings == CURLE_OK) settings = curl_easy_setopt(curl, option, value); } while(0)
    SET(CURLOPT_URL,url);
    SET(CURLOPT_PROTOCOLS_STR,"HTTPS");
    /* Follow a small number of HTTPS-only redirects. libcurl calls the
     * pre-request public-IP guard for every new connection. */
    SET(CURLOPT_FOLLOWLOCATION,1L);
    SET(CURLOPT_MAXREDIRS,3L);
    SET(CURLOPT_REDIR_PROTOCOLS_STR,"HTTPS");
    SET(CURLOPT_PROXY,""); /* no proxy or environment-derived proxy */
    SET(CURLOPT_NOPROXY,"*");
    SET(CURLOPT_NETRC,CURL_NETRC_IGNORED);
    SET(CURLOPT_HTTPAUTH,CURLAUTH_NONE);
    SET(CURLOPT_SSL_VERIFYPEER,1L);
    SET(CURLOPT_SSL_VERIFYHOST,2L);
    SET(CURLOPT_PREREQFUNCTION,deny_private_ip);
    SET(CURLOPT_CONNECTTIMEOUT,5L);
    SET(CURLOPT_TIMEOUT,20L);
    SET(CURLOPT_LOW_SPEED_LIMIT,64L);
    SET(CURLOPT_LOW_SPEED_TIME,8L);
    SET(CURLOPT_MAXFILESIZE_LARGE,(curl_off_t)ADAM_WEB_MAX_BYTES);
    SET(CURLOPT_USERAGENT,"AdamAI-web/0.1 (+https://github.com/adamcir/AdamAI)");
    SET(CURLOPT_ACCEPT_ENCODING,"");
    SET(CURLOPT_WRITEFUNCTION,receive_body);
    SET(CURLOPT_WRITEDATA,&b);
    SET(CURLOPT_HEADERFUNCTION,receive_header);
    SET(CURLOPT_HEADERDATA,&b);
    SET(CURLOPT_XFERINFOFUNCTION,progress);
    SET(CURLOPT_XFERINFODATA,cancelled);
    SET(CURLOPT_NOPROGRESS,0L);
    SET(CURLOPT_NOSIGNAL,1L);
#undef SET
    if (settings != CURLE_OK) {
        errorf(error,error_size,"libcurl option error: %s",curl_easy_strerror(settings));
        curl_easy_cleanup(curl);free(b.data);return false;
    }
    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    char *mime = NULL;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_getinfo(curl,CURLINFO_CONTENT_TYPE,&mime);
    bool ok = code == CURLE_OK && status >= 200 && status < 300 &&
              (!mime || content_type_allowed(mime));
    if (ok) {
        char *effective=NULL;
        curl_easy_getinfo(curl,CURLINFO_EFFECTIVE_URL,&effective);
        const char *actual=effective ? effective : url;
        if(!adam_web_validate_url(actual,error,error_size)) ok=false;
        if(ok) {
            page->final_url=strdup(actual);
            bool html = mime && (prefix_case(mime,"text/html") ||
                                 prefix_case(mime,"application/xhtml+xml"));
            page->text = adam_web_extract_text(b.data,b.len,html,&page->truncated);
            page->status = status;
            ok = page->text != NULL && page->final_url != NULL;
            if(ok && html) {
                char *links=adam_web_extract_links(b.data,b.len,actual);
                if(links) {
                    const char *prefix="\n\nLinks for further browsing:\n";
                    size_t link_len=strlen(links),prefix_len=strlen(prefix);
                    if(link_len+prefix_len+1<ADAM_WEB_MAX_TEXT) {
                        size_t original=strlen(page->text);
                        size_t remaining=ADAM_WEB_MAX_TEXT-link_len-prefix_len-1;
                        if(original>remaining) {
                            /* Avoid splitting UTF-8 code points when trimming
                             * article text to make room for its links. */
                            while(remaining>0 && (((unsigned char)page->text[remaining]&0xC0)==0x80))
                                --remaining;
                            page->text[remaining]='\0';
                            page->truncated=true;
                            original=remaining;
                        }
                        char *combined=realloc(page->text,original+prefix_len+link_len+1);
                        if(combined) {
                            page->text=combined;
                            memcpy(page->text+original,prefix,prefix_len);
                            memcpy(page->text+original+prefix_len,links,link_len+1);
                        }
                    }
                    free(links);
                }
            }
        }
    } else if (cancelled && atomic_load(cancelled))
        errorf(error,error_size,"Cancelled");
    else if (b.oversized) errorf(error,error_size,"Page exceeds 512 KiB limit");
    else if (b.unsupported || (mime && !content_type_allowed(mime)))
        errorf(error,error_size,"Only text, HTML, and JSON pages are supported");
    else if (status >= 300 && status < 400)
        errorf(error,error_size,"HTTP %ld redirect blocked; use its HTTPS destination",status);
    else if (code != CURLE_OK)
        errorf(error,error_size,"Web request failed: %s",curl_easy_strerror(code));
    else errorf(error,error_size,"Web request returned HTTP %ld",status);
    curl_easy_cleanup(curl);
    free(b.data);
    return ok;
}
void adam_web_page_free(AdamWebPage *page) {
    if (!page) return;
    free(page->text);
    free(page->final_url);
    memset(page,0,sizeof(*page));
}

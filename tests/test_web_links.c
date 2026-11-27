/* AdamAI - GPL-3.0-only */
#include "adam/web_links.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    const char *html="<a href='/docs/test'>Docs</a>"
      "<a href=\"https://example.org/ref?q=1&amp;a=2\">Ref</a>"
      "<a href='javascript:alert(1)'>No</a>"
      "<a href='//duckduckgo.com/l/?uddg=https%3A%2F%2Fdocs.example.net%2Fguide'>Result</a>"
      "<a href='http://insecure.example/path'>No</a>";
    char *links=adam_web_extract_links(html,strlen(html),"https://example.com/overview");
    assert(links);
    assert(strstr(links,"https://example.com/docs/test"));
    assert(strstr(links,"https://example.org/ref?q=1&a=2"));
    assert(strstr(links,"https://docs.example.net/guide"));
    assert(!strstr(links,"javascript:"));
    assert(!strstr(links,"http://"));
    puts(links);
    free(links);
    return 0;
}
/* This local test stubs the full web_fetch network implementation. */
#ifdef ADAM_TEST_WEB_VALIDATION_STUB
bool adam_web_validate_url(const char *s, char *error, size_t cap) {
    (void)error;(void)cap;
    return s && strncmp(s,"https://",8)==0 && !strchr(s+8,'@');
}

#endif

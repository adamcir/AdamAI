/* AdamAI - GPL-3.0-only */
#include "adam/web_fetch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    char e[256] = "";
    assert(adam_web_validate_url("https://example.com/a",e,sizeof(e)));
    assert(adam_web_validate_url("https://example.com:443/a",e,sizeof(e)));
    assert(!adam_web_validate_url("file:///etc/passwd",e,sizeof(e)));
    assert(!adam_web_validate_url("http://example.com",e,sizeof(e)));
    assert(!adam_web_validate_url("https://u:pass@example.com/",e,sizeof(e)));
    assert(!adam_web_validate_url("https://localhost/",e,sizeof(e)));
    assert(!adam_web_validate_url("https://127.0.0.1/",e,sizeof(e)));
    assert(!adam_web_validate_url("https://192.168.1.1/",e,sizeof(e)));
    assert(!adam_web_validate_url("https://example.com:8443/",e,sizeof(e)));
    assert(!adam_web_validate_url("https://example.com/a\r\nX-Test:yes",e,sizeof(e)));
    assert(adam_web_public_ip("1.1.1.1"));
    assert(adam_web_public_ip("8.8.8.8"));
    assert(!adam_web_public_ip("10.0.0.1"));
    assert(!adam_web_public_ip("172.16.0.1"));
    assert(!adam_web_public_ip("169.254.169.254"));
    assert(!adam_web_public_ip("100.64.0.1"));
    assert(!adam_web_public_ip("127.0.0.1"));
    assert(!adam_web_public_ip("192.0.2.1"));
    assert(!adam_web_public_ip("203.0.113.1"));
    assert(!adam_web_public_ip("::1"));
    assert(!adam_web_public_ip("fe80::1"));
    assert(!adam_web_public_ip("::ffff:127.0.0.1"));
    assert(!adam_web_public_ip("2001:db8::1"));
    assert(adam_web_public_ip("2606:4700:4700::1111"));
    const char *sample="<html><style>SECRET</style><script>doBad()</script>"
                        "<h1>Title</h1><p>Hello &amp; world!</p><p>Příliš dobré &#33;</p></html>";
    bool truncated = true;
    char *text = adam_web_extract_text(sample,strlen(sample),true,&truncated);
    assert(text);
    assert(strstr(text,"Title"));
    assert(strstr(text,"Hello & world!"));
    assert(strstr(text,"Příliš dobré !"));
    assert(!strstr(text,"SECRET"));
    assert(!strstr(text,"doBad"));
    assert(!truncated);
    free(text);
    puts("web fetch tests passed");
    return 0;
}

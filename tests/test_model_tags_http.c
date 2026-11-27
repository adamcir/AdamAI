/* AdamAI - GPL-3.0-only */
#include "adam/model_discovery.h"
#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    assert(argc==2);
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    char selected[128]={0},error[180]={0};
    assert(adam_model_resolve(argv[1],"adam1",selected,sizeof(selected),error,sizeof(error)));
    assert(!strcmp(selected,"adam1:1.0"));
    assert(adam_model_resolve(argv[1],"adam1:*",selected,sizeof(selected),error,sizeof(error)));
    assert(!strcmp(selected,"adam1:1.0"));
    assert(adam_model_resolve(argv[1],"adam1:1.0",selected,sizeof(selected),error,sizeof(error)));
    assert(!strcmp(selected,"adam1:1.0"));
    curl_global_cleanup();
    puts("Ollama /api/tags model discovery passed");
    return 0;
}

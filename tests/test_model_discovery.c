/* AdamAI - GPL-3.0-only */
#include "adam/model_discovery.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char s[128];
    assert(adam_model_select_from_tags("{\"models\":[{\"name\":\"qwen3:8b\"},{\"name\":\"adam1:1.0\"}]}","adam1",s,sizeof(s)));
    assert(!strcmp(s,"adam1:1.0"));
    assert(adam_model_select_from_tags("{\"models\":[{\"name\":\"adam1:1.9\"},{\"name\":\"adam1:1.10\"},{\"name\":\"adam1:latest\"}]}","adam1:*",s,sizeof(s)));
    assert(!strcmp(s,"adam1:1.10"));
    assert(adam_model_select_from_tags("{\"models\":[{\"model\":\"adam1:2.0\"}]}","adam1",s,sizeof(s)));
    assert(!strcmp(s,"adam1:2.0"));
    assert(!adam_model_select_from_tags("{\"models\":[{\"name\":\"adam10:1.0\"}]}","adam1",s,sizeof(s)));
    assert(!adam_model_select_from_tags("not json","adam1",s,sizeof(s)));
    puts("Model auto-discovery tests passed");
    return 0;
}

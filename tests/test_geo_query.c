/* AdamAI - GPL-3.0-only */
#include "adam/tool_registry.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    char *a=adam_geo_search_url("Stěžery");
    assert(a);
    assert(!strcmp(a,
      "https://nominatim.openstreetmap.org/search?format=jsonv2&addressdetails=1&limit=5&q=St%C4%9B%C5%BEery"));
    free(a);
    char *b=adam_geo_search_url("Stěžery Višňová");
    assert(b);
    assert(strstr(b,"q=St%C4%9B%C5%BEery%20Vi%C5%A1%C5%88ov%C3%A1"));
    free(b);
    assert(adam_geo_search_url("")==NULL);
    assert(adam_geo_search_url(NULL)==NULL);
    assert(adam_tools_is_network("geo_search"));
    assert(adam_tools_known("geo_search"));
    return 0;
}

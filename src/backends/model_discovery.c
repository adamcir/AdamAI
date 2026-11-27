/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/model_discovery.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define TAGS_RESPONSE_LIMIT (1024u * 1024u)

typedef struct { char *buffer; size_t length; } Tags;

static size_t tags_receiver(char *ptr, size_t size, size_t count, void *userdata) {
    Tags *t=userdata;
    if(!size || count>SIZE_MAX/size) return 0;
    size_t n=size*count;
    if(n>TAGS_RESPONSE_LIMIT-t->length) return 0;
    memcpy(t->buffer+t->length,ptr,n);
    t->length+=n;
    t->buffer[t->length]='\0';
    return n;
}

static bool numeric_version(const char *tag) {
    if(!tag || !isdigit((unsigned char)*tag)) return false;
    for(const unsigned char *p=(const unsigned char *)tag;*p;++p)
        if(!isdigit(*p) && *p!='.') return false;
    return true;
}
static int compare_numeric_tags(const char *a, const char *b) {
    while(*a || *b) {
        unsigned long va=0, vb=0;
        while(isdigit((unsigned char)*a)) { if(va<1000000000ul) va=va*10+(*a-'0'); ++a; }
        while(isdigit((unsigned char)*b)) { if(vb<1000000000ul) vb=vb*10+(*b-'0'); ++b; }
        if(va!=vb) return va>vb ? 1 : -1;
        if(*a=='.')++a;
        if(*b=='.')++b;
    }
    return 0;
}
static int tag_rank(const char *tag) {
    if(numeric_version(tag)) return 3;
    if(!strcmp(tag,"latest")) return 2;
    if(!*tag) return 1;
    return 0;
}
static bool better_match(const char *candidate, const char *best, size_t prefix_len) {
    if(!best || !*best) return true;
    const char *a=candidate+prefix_len, *b=best+prefix_len;
    if(*a==':') ++a;
    if(*b==':') ++b;
    int ra=tag_rank(a), rb=tag_rank(b);
    if(ra!=rb) return ra>rb;
    if(ra==3) return compare_numeric_tags(a,b)>0;
    return strcmp(candidate,best)>0;
}

bool adam_model_select_from_tags(const char *tags_json, const char *requested,
                                 char *result, size_t result_cap) {
    if(!tags_json || !requested || !*requested || !result || result_cap<2) return false;
    result[0]='\0';
    size_t prefix_len=strlen(requested);
    bool wildcard=prefix_len>=2 && !strcmp(requested+prefix_len-2,":*");
    if(wildcard) prefix_len-=2;
    else if(strchr(requested,':')) return false;
    if(!prefix_len || prefix_len>=result_cap) return false;
    struct json_object *root=json_tokener_parse(tags_json),*models=NULL;
    if(!root || !json_object_object_get_ex(root,"models",&models) ||
       !json_object_is_type(models,json_type_array)) {
        if(root)json_object_put(root);
        return false;
    }
    bool found=false;
    size_t count=json_object_array_length(models);
    for(size_t i=0;i<count;++i) {
        struct json_object *item=json_object_array_get_idx(models,i),*name_obj=NULL;
        if(!item || !json_object_is_type(item,json_type_object))continue;
        if(!json_object_object_get_ex(item,"name",&name_obj))
            json_object_object_get_ex(item,"model",&name_obj);
        if(!name_obj || !json_object_is_type(name_obj,json_type_string))continue;
        const char *name=json_object_get_string(name_obj);
        size_t len=json_object_get_string_len(name_obj);
        if(!name || len>=result_cap || strncmp(name,requested,prefix_len)!=0 ||
           !(name[prefix_len]=='\0' || name[prefix_len]==':') ||
           len!=strlen(name))continue;
        if(!found || better_match(name,result,prefix_len)) {
            memcpy(result,name,len+1);
            found=true;
        }
    }
    json_object_put(root);
    return found;
}

bool adam_model_resolve(const char *ollama_endpoint, const char *requested,
                        char *result, size_t result_cap,
                        char *error, size_t error_cap) {
    if(error && error_cap)error[0]='\0';
    if(!ollama_endpoint || !requested || !*requested || !result || !result_cap)return false;
    if(strchr(requested,':') && !strstr(requested,":*")) {
        if(strlen(requested)>=result_cap)return false;
        snprintf(result,result_cap,"%s",requested);
        return true;
    }
    size_t root_len=strlen(ollama_endpoint);
    while(root_len>0 && ollama_endpoint[root_len-1]=='/')--root_len;
    if(root_len>2048 || root_len<7)return false;
    char url[2112];
    snprintf(url,sizeof(url),"%.*s/api/tags",(int)root_len,ollama_endpoint);
    Tags t={.buffer=malloc(TAGS_RESPONSE_LIMIT+1)};
    if(!t.buffer)return false;
    t.buffer[0]='\0';
    CURL *curl=curl_easy_init();
    if(!curl){free(t.buffer);return false;}
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_HTTPGET,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,600L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,1800L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,tags_receiver);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&t);
    CURLcode code=curl_easy_perform(curl);
    long status=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(curl);
    bool ok=code==CURLE_OK && status==200 &&
            adam_model_select_from_tags(t.buffer,requested,result,result_cap);
    if(!ok && error && error_cap) {
        if(code!=CURLE_OK)
            snprintf(error,error_cap,"Ollama /api/tags: %s",curl_easy_strerror(code));
        else if(status!=200)
            snprintf(error,error_cap,"Ollama /api/tags: HTTP %ld",status);
        else snprintf(error,error_cap,"Model %s:* not found; check ollama list",requested);
    }
    free(t.buffer);
    return ok;
}

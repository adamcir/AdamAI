/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/web_links.h"
#include "adam/web_fetch.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINK_LIST_CAP 3600u
#define LINK_COUNT_CAP 24u

static int hexval(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='A'&&c<='F')return c-'A'+10;
    if(c>='a'&&c<='f')return c-'a'+10;
    return -1;
}
static void decode_percent(char *s) {
    char *read=s,*write=s;
    while(*read) {
        if(read[0]=='%' && read[1] && read[2]) {
            int a=hexval(read[1]), b=hexval(read[2]);
            if(a>=0 && b>=0) {
                unsigned char decoded=(unsigned char)((a<<4)|b);
                if(decoded<32 || decoded==127){ *write++='?'; }
                else *write++=(char)decoded;
                read+=3;continue;
            }
        }
        *write++=*read++;
    }
    *write='\0';
}
/* Unwrap DuckDuckGo's public search redirect, keeping only HTTPS target. */
static char *unwrap_search_result(const char *url) {
    const char *key=strstr(url,"uddg=");
    if(!key)return strdup(url);
    if(key>url && key[-1]!='?' && key[-1]!='&')return strdup(url);
    key+=5;
    size_t n=strcspn(key,"&#");
    if(!n || n>2048)return strdup(url);
    char *unwrapped=strndup(key,n);
    if(!unwrapped)return NULL;
    decode_percent(unwrapped);
    if(adam_web_validate_url(unwrapped,NULL,0))return unwrapped;
    free(unwrapped);
    return strdup(url);
}
static bool prefix_case(const char *a, const char *b, size_t n) {
    for(size_t i=0;i<n;++i)
        if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i]))return false;
    return true;
}
static void html_amp_decode(char *s) {
    char *r=s,*w=s;
    while(*r) {
        if(!strncmp(r,"&amp;",5)){*w++='&';r+=5;}
        else {*w++=*r++;}
    }
    *w=0;
}
char *adam_web_extract_links(const char *html, size_t size, const char *base_url) {
    if(!html || !base_url || !adam_web_validate_url(base_url,NULL,0))return NULL;
    char *out=calloc(LINK_LIST_CAP+1,1);
    if(!out)return NULL;
    size_t used=0, count=0;
    for(size_t i=0;i<size && count<LINK_COUNT_CAP && used<LINK_LIST_CAP-360;++i) {
        if(html[i]!='<' || i+2>=size || tolower((unsigned char)html[i+1])!='a' ||
           !(isspace((unsigned char)html[i+2]) || html[i+2]=='>'))continue;
        size_t end=i+2;
        while(end<size && end-i<4096 && html[end]!='>')++end;
        if(end>=size || html[end]!='>')continue;
        /* Scan HTML anchor attributes, not text between tags. */
        for(size_t pos=i+2;pos<end;) {
            while(pos<end && isspace((unsigned char)html[pos]))++pos;
            size_t begin=pos;
            while(pos<end && (isalnum((unsigned char)html[pos]) || html[pos]=='-' || html[pos]=='_'))++pos;
            size_t n=pos-begin;
            if(!n){++pos;continue;}
            while(pos<end && isspace((unsigned char)html[pos]))++pos;
            if(pos>=end || html[pos]!='=')continue;
            ++pos;
            while(pos<end && isspace((unsigned char)html[pos]))++pos;
            char quote=0;
            if(pos<end && (html[pos]=='\'' || html[pos]=='"'))quote=html[pos++];
            size_t value_start=pos;
            while(pos<end && (quote? html[pos]!=quote :
                      !isspace((unsigned char)html[pos]) && html[pos]!='>'))++pos;
            size_t value_len=pos-value_start;
            if(quote && pos<end)++pos;
            if(n!=4 || !prefix_case(html+begin,"href",4) ||
               value_len==0 || value_len>1000)continue;
            char *href=strndup(html+value_start,value_len);
            if(!href)break;
            html_amp_decode(href);
            CURLU *u=curl_url();
            if(!u) {free(href);break;}
            if(curl_url_set(u,CURLUPART_URL,base_url,0)!=CURLUE_OK ||
               curl_url_set(u,CURLUPART_URL,href,0)!=CURLUE_OK) {
                curl_url_cleanup(u);free(href);break;
            }
            char *resolved=NULL;
            if(curl_url_get(u,CURLUPART_URL,&resolved,0)==CURLUE_OK && resolved) {
                char *clean=unwrap_search_result(resolved);
                if(clean && strlen(clean)<=320 &&
                   adam_web_validate_url(clean,NULL,0) &&
                   (!used || !strstr(out,clean))) {
                    size_t l=strlen(clean);
                    size_t room=LINK_LIST_CAP-used;
                    if(l+6<=room) {
                        int wrote=snprintf(out+used,room+1,"%zu. %s\n",count+1,clean);
                        if(wrote>0 && (size_t)wrote<=room) {used+=(size_t)wrote; ++count;}
                    }
                }
                free(clean);
                curl_free(resolved);
            }
            curl_url_cleanup(u);
            free(href);
            break;
        }
        i=end;
    }
    if(!count){free(out);return NULL;}
    return out;
}

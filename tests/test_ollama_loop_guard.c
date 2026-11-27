/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/ollama.h"
#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int main(int argc,char **argv){
    assert(argc==2);
    char endpoint[128];
    snprintf(endpoint,sizeof(endpoint),"http://127.0.0.1:%s",argv[1]);
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    AdamChat chat;
    adam_chat_init(&chat);
    assert(adam_chat_add(&chat,ADAM_ROLE_USER,"Read a place"));
    AdamStream stream;
    assert(adam_ollama_start(&stream,&chat,"test",endpoint,"off",true,false,NULL));
    int done=0,errors=0,final=0,progress=0;
    for(int i=0;i<800 && !done;i++){
        AdamStreamEvent *ev;
        while((ev=adam_ollama_poll(&stream))){
            if(ev->type==ADAM_STREAM_ERROR){fprintf(stderr,"ERROR: %s\n",ev->text);errors++;}
            if(ev->type==ADAM_STREAM_TEXT && ev->text &&
               strstr(ev->text,"Unable to verify"))final++;
            if(ev->type==ADAM_STREAM_TOOL)progress++;
            if(ev->type==ADAM_STREAM_DONE)done++;
            free(ev->text);free(ev);
        }
        if(!done){
            const struct timespec delay={.tv_sec=0,.tv_nsec=10000000L};
            nanosleep(&delay,NULL);
        }
    }
    adam_ollama_stop(&stream);
    adam_chat_clear(&chat);
    curl_global_cleanup();
    assert(done==1 && errors==0 && final==1 && progress>=1);
    puts("Agent repeated tool-call guard passed");
    return 0;
}

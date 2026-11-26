/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/ollama.h"
#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv) {
    assert(argc==2);
    char root[]="/tmp/adamai-loop-XXXXXX";
    assert(mkdtemp(root));
    char path[512];snprintf(path,sizeof(path),"%s/note.txt",root);
    FILE *f=fopen(path,"wb");assert(f);assert(fputs("original\n",f)>=0);assert(!fclose(f));
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    char endpoint[128];snprintf(endpoint,sizeof(endpoint),"http://127.0.0.1:%s",argv[1]);
    AdamChat chat;adam_chat_init(&chat);
    assert(adam_chat_add(&chat,ADAM_ROLE_USER,"Read the project and update note.txt")!=NULL);
    AdamStream stream;
    assert(adam_ollama_start(&stream,&chat,"test",endpoint,"off",true,true,root));
    int tool=0,txt=0,err=0,done=0,approved=0;
    for(int i=0;i<800 && !done;i++){
        AdamStreamEvent *ev;
        while((ev=adam_ollama_poll(&stream))){
            if(ev->type==ADAM_STREAM_TOOL)++tool;
            if(ev->type==ADAM_STREAM_APPROVAL){
                assert(ev->text);
                assert(strstr(ev->text,"Target: note.txt"));
                assert(strstr(ev->text,"updated by AdamAI"));
                assert(adam_ollama_answer_approval(&stream,true));
                ++approved;
            }
            if(ev->type==ADAM_STREAM_TEXT && ev->text && strstr(ev->text,"final"))++txt;
            if(ev->type==ADAM_STREAM_ERROR){fprintf(stderr,"ERR: %s\n",ev->text);++err;}
            if(ev->type==ADAM_STREAM_DONE)++done;
            free(ev->text);free(ev);
        }
        if(!done){const struct timespec delay={.tv_sec=0,.tv_nsec=10000000L};nanosleep(&delay,NULL);}
    }
    adam_ollama_stop(&stream);adam_chat_clear(&chat);curl_global_cleanup();
    f=fopen(path,"rb");assert(f);char buf[256]={0};assert(fgets(buf,sizeof(buf),f));assert(!fclose(f));
    assert(!strcmp(buf,"updated by AdamAI\n"));
    assert(tool>=3 && txt==1 && err==0 && done==1 && approved==1);
    assert(unlink(path)==0);assert(rmdir(root)==0);
    puts("FS Ollama tool loop passed");return 0;
}

/* AdamAI - GPL-3.0-only
 * Local fake Ollama: workspace selection must ask user, as must outbound web
 * requests when FS is active. No actual external request is made. */
#define _POSIX_C_SOURCE 200809L
#include "adam/ollama.h"
#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv) {
    assert(argc==3);
    char parent[]="/tmp/adamai-permissions-XXXXXX";
    assert(mkdtemp(parent));
    char a[512],b[512];
    snprintf(a,sizeof(a),"%s/projA",parent);
    snprintf(b,sizeof(b),"%s/projB",parent);
    assert(mkdir(a,0700)==0 && mkdir(b,0700)==0);
    /* The mock server requests a separately prepared existing folder. */
    const char *target=argv[2];
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    char endpoint[128];
    snprintf(endpoint,sizeof(endpoint),"http://127.0.0.1:%s",argv[1]);

    AdamChat chat;adam_chat_init(&chat);
    assert(adam_chat_add(&chat,ADAM_ROLE_USER,"Work with a second project and check a website"));
    AdamStream stream;
    assert(adam_ollama_start(&stream,&chat,"test",endpoint,"off",true,true,a));
    int prompts=0,workspace_updates=0,final=0,errors=0,done=0;
    for(int i=0;i<800 && !done;i++) {
        AdamStreamEvent *ev;
        while((ev=adam_ollama_poll(&stream))) {
            if(ev->type==ADAM_STREAM_APPROVAL) {
                ++prompts;
                if(prompts==1) {
                    assert(strstr(ev->text,"WORKSPACE CHANGE"));
                    assert(strstr(ev->text,target));
                    assert(adam_ollama_answer_approval(&stream,true));
                } else if(prompts==2) {
                    assert(strstr(ev->text,"OUTBOUND WEB"));
                    assert(strstr(ev->text,"https://example.com/?private=do-not-send"));
                    assert(adam_ollama_answer_approval(&stream,false));
                } else abort();
            } else if(ev->type==ADAM_STREAM_WORKSPACE) {
                assert(ev->text && !strcmp(ev->text,target));
                workspace_updates++;
            } else if(ev->type==ADAM_STREAM_TEXT && ev->text &&
                      strstr(ev->text,"final")) final++;
            else if(ev->type==ADAM_STREAM_ERROR) {
                fprintf(stderr,"Error: %s\n",ev->text);errors++;
            } else if(ev->type==ADAM_STREAM_DONE) done++;
            free(ev->text);free(ev);
        }
        if(!done) {
            const struct timespec delay={.tv_sec=0,.tv_nsec=10000000L};
            nanosleep(&delay,NULL);
        }
    }
    adam_ollama_stop(&stream);
    adam_chat_clear(&chat);
    curl_global_cleanup();
    assert(prompts==2 && workspace_updates==1 && final==1 &&
           errors==0 && done==1);
    assert(rmdir(a)==0 && rmdir(b)==0 && rmdir(parent)==0);
    puts("Workspace and web approvals passed");
    return 0;
}

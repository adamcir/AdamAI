/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/ollama.h"
#include <assert.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "http://127.0.0.1:%s", argv[1]);
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    AdamChat chat; adam_chat_init(&chat);
    assert(adam_chat_add(&chat, ADAM_ROLE_USER, "Read the site") != NULL);
    AdamStream stream;
    assert(adam_ollama_start(&stream,&chat,"test",endpoint,"off",true,false,NULL));
    int tool=0,txt=0,err=0,done=0;
    for (int i=0;i<500 && !done;i++) {
        AdamStreamEvent *ev;
        while ((ev=adam_ollama_poll(&stream))) {
            if (ev->type==ADAM_STREAM_TOOL) ++tool;
            if (ev->type==ADAM_STREAM_TEXT && ev->text && strstr(ev->text,"final")) ++txt;
            if (ev->type==ADAM_STREAM_ERROR) {fprintf(stderr,"ERR: %s\n",ev->text);++err;}
            if (ev->type==ADAM_STREAM_DONE) ++done;
            free(ev->text);free(ev);
        }
        if (!done) {
            const struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000L};
            nanosleep(&delay, NULL);
        }
    }
    adam_ollama_stop(&stream);adam_chat_clear(&chat);curl_global_cleanup();
    assert(tool>=1);assert(txt==1);assert(err==0);assert(done==1);
    puts("Ollama web tool loop passed");return 0;
}

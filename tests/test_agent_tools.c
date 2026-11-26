/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/tool_registry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
static bool approve_yes(const char *text,void *arg){int *calls=arg;assert(strstr(text,"APPROVAL"));++*calls;return true;}
static bool approve_no(const char *text,void *arg){int *calls=arg;assert(strstr(text,"APPROVAL"));++*calls;return false;}
static void run(AdamToolContext *ctx,const char *name,const char *json,bool expect,const char *contains){
 struct json_object *arg=json_tokener_parse(json);assert(arg);
 char *out=NULL,error[256]="";
 bool ok=adam_tools_execute(name,arg,ctx,&out,error,sizeof(error));
 if(ok!=expect){fprintf(stderr,"Tool %s: %s\n",name,error);abort();}
 if(expect && contains && (!out||!strstr(out,contains))){fprintf(stderr,"Tool %s: missing %s in %s\n",name,contains,out?out:"(null)");abort();}
 free(out);json_object_put(arg);
}
int main(void){
 char root[]="/tmp/adamai-agent-XXXXXX";assert(mkdtemp(root));
 char path[512];snprintf(path,sizeof(path),"%s/test.c",root);
 FILE *f=fopen(path,"w");assert(f);fputs("int alpha(void) { return 7; }\n",f);assert(!fclose(f));
 AdamFs fs;char error[256]="";assert(adam_fs_init(&fs,root,error,sizeof(error)));
 int calls=0;
 AdamToolContext ctx={.fs=&fs,.approve=approve_no,.approval_userdata=&calls};
 struct json_object *schemas=adam_tools_schemas();assert(schemas);
 assert(json_object_array_length(schemas)>=20);json_object_put(schemas);
 run(&ctx,"find_files","{\"query\":\"test.c\"}",true,"test.c");
 run(&ctx,"search_files","{\"query\":\"alpha\"}",true,"test.c:1:");
 run(&ctx,"edit_file","{\"path\":\"test.c\",\"old_text\":\"return 7\",\"new_text\":\"return 9\"}",false,NULL);
 f=fopen(path,"r");assert(f);char line[128];assert(fgets(line,sizeof(line),f));assert(strstr(line,"return 7"));fclose(f);
 assert(calls==1);ctx.approve=approve_yes;
 run(&ctx,"edit_file","{\"path\":\"test.c\",\"old_text\":\"return 7\",\"new_text\":\"return 9\"}",true,"written");
 run(&ctx,"create_directory","{\"path\":\"subdir\"}",true,"completed");
 run(&ctx,"move_file","{\"from\":\"test.c\",\"to\":\"subdir/moved.c\"}",true,"completed");
 run(&ctx,"delete_file","{\"path\":\"subdir/moved.c\"}",true,"completed");
 run(&ctx,"create_directory","{\"path\":\"../outside\"}",false,NULL);
 run(&ctx,"hardware_info","{}",true,"AdamAI hardware");
 assert(rmdir(strcat(strcpy(path,root),"/subdir"))==0);
 adam_fs_close(&fs);assert(rmdir(root)==0);
 puts("Agent registry, scoped search, edits and approvals passed");return 0;
}

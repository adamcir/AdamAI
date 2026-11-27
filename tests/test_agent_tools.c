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
 assert(json_object_array_length(schemas)>=32);json_object_put(schemas);
 assert(adam_tools_is_network("web_search"));
 assert(!adam_tools_is_network("git_status"));
 run(&ctx,"find_files","{\"query\":\"test.c\"}",true,"test.c");
 run(&ctx,"search_files","{\"query\":\"alpha\"}",true,"test.c:1:");
 run(&ctx,"edit_file","{\"path\":\"test.c\",\"old_text\":\"return 7\",\"new_text\":\"return 9\"}",false,NULL);
 f=fopen(path,"r");assert(f);char line[128];assert(fgets(line,sizeof(line),f));assert(strstr(line,"return 7"));fclose(f);
 assert(calls==1);ctx.approve=approve_yes;
 assert(setenv("HOME",root,1)==0);
 run(&ctx,"edit_file","{\"path\":\"test.c\",\"old_text\":\"return 7\",\"new_text\":\"return 9\"}",true,"written");
 run(&ctx,"create_directory","{\"path\":\"subdir\"}",true,"completed");
 run(&ctx,"move_file","{\"from\":\"test.c\",\"to\":\"subdir/moved.c\"}",true,"completed");
 run(&ctx,"delete_file","{\"path\":\"subdir/moved.c\"}",true,"completed");
 run(&ctx,"create_directory","{\"path\":\"../outside\"}",false,NULL);
 run(&ctx,"hardware_info","{}",true,"AdamAI hardware");
 if(geteuid()!=0){
     ctx.allow_processes=true;
     run(&ctx,"execute_command",
         "{\"argv\":[\"echo\",\"adamai-terminal-test\"]}",
         true,"adamai-terminal-test");
     ctx.approve=approve_no;
     run(&ctx,"execute_command",
         "{\"argv\":[\"echo\",\"must-not-run\"]}",
         false,NULL);
     ctx.approve=approve_yes;
     run(&ctx,"execute_command",
         "{\"argv\":[\"sh\",\"-c\",\"echo unsafe\"]}",
         false,NULL);
 }
 ctx.approve=approve_no;
 run(&ctx,"memory_update","{\"note\":\"Remember C17 coding style\"}",false,NULL);
 ctx.approve=approve_yes;
 run(&ctx,"memory_update","{\"note\":\"Remember C17 coding style\"}",true,"saved");
 run(&ctx,"memory_search","{\"query\":\"C17\"}",true,"C17 coding style");
 run(&ctx,"task_manager","{\"action\":\"add\",\"text\":\"Verify GitHub CI\"}",true,"Task updated");
 run(&ctx,"task_manager","{\"action\":\"list\"}",true,"Verify GitHub CI");
 run(&ctx,"web_extract","{\"url\":\"https://example.com/\"}",false,NULL);

 assert(rmdir(strcat(strcpy(path,root),"/subdir"))==0);
 adam_fs_close(&fs);
 char mem[512],tasks[512],folder[512];
 snprintf(mem,sizeof(mem),"%s/adam-ai/memory.jsonl",root);
 snprintf(tasks,sizeof(tasks),"%s/adam-ai/tasks.jsonl",root);
 snprintf(folder,sizeof(folder),"%s/adam-ai",root);
 assert(unlink(mem)==0);assert(unlink(tasks)==0);assert(rmdir(folder)==0);assert(rmdir(root)==0);
 puts("Agent registry, scoped search, edits and approvals passed");return 0;
}

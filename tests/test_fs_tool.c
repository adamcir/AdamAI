#define _POSIX_C_SOURCE 200809L
#include "adam/fs_tool.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
int main(void){
 char tmp[]="/tmp/adamai-fs-test-XXXXXX";assert(mkdtemp(tmp));
 char sub[1024],link[1024],plain[1024];
 snprintf(sub,sizeof(sub),"%s/sub",tmp);assert(mkdir(sub,0700)==0);
 snprintf(link,sizeof(link),"%s/sub/outside",tmp);assert(symlink("/etc",link)==0);
 snprintf(plain,sizeof(plain),"%s/sub/a.c",tmp);
 AdamFs fs={0};char error[256]="";assert(adam_fs_init(&fs,tmp,error,sizeof(error)));
 assert(adam_fs_valid_path("sub/a.c",false));
 assert(!adam_fs_valid_path("../etc/passwd",false));
 assert(!adam_fs_valid_path("sub/../etc",false));
 assert(!adam_fs_valid_path("/etc/passwd",false));
 assert(!adam_fs_valid_path("sub//test",false));
 assert(adam_fs_valid_path(".",true));
 const char *source="int main(void) { return 0; }\n";
 assert(adam_fs_write(&fs,"sub/a.c",source,strlen(source),error,sizeof(error)));
 char *txt=NULL;assert(adam_fs_read(&fs,"sub/a.c",&txt,error,sizeof(error)));
 assert(!strcmp(txt,source));free(txt);
 assert(!adam_fs_read(&fs,"sub/outside/passwd",&txt,error,sizeof(error)));
 assert(!adam_fs_write(&fs,"sub/outside/shadow","test",4,error,sizeof(error)));
 assert(!adam_fs_read(&fs,"sub/outside",&txt,error,sizeof(error)));
 assert(adam_fs_list(&fs,"sub",&txt,error,sizeof(error)));
 assert(strstr(txt,"[f] a.c")&&strstr(txt,"[l] outside"));free(txt);
 bool exists=false;size_t size=0;
 assert(adam_fs_target_info(&fs,"sub/a.c",&exists,&size,error,sizeof(error))&&exists&&size==strlen(source));
 assert(!adam_fs_target_info(&fs,"sub/outside",&exists,&size,error,sizeof(error)));
 const char invalid[]="a\0b";
 assert(!adam_fs_write(&fs,"sub/bin",invalid,3,error,sizeof(error)));
 assert(!adam_fs_write(&fs,"sub/../../escape","y",1,error,sizeof(error)));
 adam_fs_close(&fs);
 assert(unlink(plain)==0);assert(unlink(link)==0);assert(rmdir(sub)==0);assert(rmdir(tmp)==0);
 puts("fs tool tests passed");return 0;
}

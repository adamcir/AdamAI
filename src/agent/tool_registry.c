/* AdamAI - GPL-3.0-only */
#define _GNU_SOURCE
#include "adam/tool_registry.h"
#include "adam/web_fetch.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SEARCH_HITS 64
#define SEARCH_DEPTH 4
#define PROCESS_MAX_OUTPUT 24576
#define PROCESS_MAX_ARGS 24
#define TOOL_REPLY_MAX 32768

typedef struct { const char *name,*description,*parameters; } Spec;
#define PARAM1(k,desc) "{\"type\":\"object\",\"required\":[\"" k "\"],\"properties\":{\"" k "\":{\"type\":\"string\",\"description\":\"" desc "\"}}}"
#define PARAM2(k1,d1,k2,d2) "{\"type\":\"object\",\"required\":[\"" k1 "\",\"" k2 "\"],\"properties\":{\"" k1 "\":{\"type\":\"string\",\"description\":\"" d1 "\"},\"" k2 "\":{\"type\":\"string\",\"description\":\"" d2 "\"}}}"
static const Spec specs[]={
 {"find_files","Find project files by filename substring, without following symbolic links.",PARAM1("query","Part of filename to match")},
 {"search_files","Search project source files for a literal substring; bounded recursive traversal.",PARAM1("query","Exact text to find")},
 {"find_symbol","Find a function or symbol by literal source-code search (not semantic LSP).",PARAM1("symbol","C or C++ symbol name")},
 {"find_references","Find references by literal source-code search.",PARAM1("symbol","C or C++ symbol name")},
 {"project_index","Count files and report project layout (on demand).","{\"type\":\"object\",\"properties\":{}}"},
 {"inspect_dependencies","Read project build dependencies from CMakeLists.txt.","{\"type\":\"object\",\"properties\":{}}"},
 {"edit_file","Replace one exact text occurrence in a file after Y/N approval.","{\"type\":\"object\",\"required\":[\"path\",\"old_text\",\"new_text\"],\"properties\":{\"path\":{\"type\":\"string\"},\"old_text\":{\"type\":\"string\"},\"new_text\":{\"type\":\"string\"}}}"},
 {"apply_patch","Apply an exact-text patch (old_text/new_text), not a unified diff; requires approval.","{\"type\":\"object\",\"required\":[\"path\",\"old_text\",\"new_text\"],\"properties\":{\"path\":{\"type\":\"string\"},\"old_text\":{\"type\":\"string\"},\"new_text\":{\"type\":\"string\"}}}"},
 {"create_directory","Create one project directory (parent must exist), after approval.",PARAM1("path","Relative directory path")},
 {"delete_file","Delete one regular file from workspace after approval.",PARAM1("path","Relative file path")},
 {"move_file","Move one regular file, without replacing an existing target, after approval.",PARAM2("from","Relative source path","to","Relative target path")},
 {"execute_command","Run an argv array with no shell, output/time limits, and mandatory approval.","{\"type\":\"object\",\"required\":[\"argv\"],\"properties\":{\"argv\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"maxItems\":24}}}"},
 {"build_project","Run cmake --build build with explicit approval (build scripts can execute code).","{\"type\":\"object\",\"properties\":{}}"},
 {"run_tests","Run ctest --test-dir build with explicit approval.","{\"type\":\"object\",\"properties\":{}}"},
 {"get_diagnostics","Run a CMake project build to collect compiler diagnostics, after approval.","{\"type\":\"object\",\"properties\":{}}"},
 {"format_code","Return clang-format output for a C/C++ file; does not change file.",PARAM1("path","Relative source filename")},
 {"git_status","Read repository status (short porcelain format).","{\"type\":\"object\",\"properties\":{}}"},
 {"git_diff","Read unstaged Git diff with external diffs disabled.","{\"type\":\"object\",\"properties\":{}}"},
 {"git_log","Read last eight git commits.","{\"type\":\"object\",\"properties\":{}}"},
 {"git_branch","List local git branches.","{\"type\":\"object\",\"properties\":{}}"},
 {"git_commit","Commit already-staged changes using a user-approved message.",PARAM1("message","Commit message")},
 {"hardware_info","Read OS, CPU and memory information from /proc. No secrets.","{\"type\":\"object\",\"properties\":{}}"},
 {"model_manager","List models through local Ollama CLI (read-only).","{\"type\":\"object\",\"properties\":{}}"},
 {"memory_search","Search user-approved local AdamAI notes in ~/adam-ai/memory.jsonl.",PARAM1("query","Text to search in stored notes")},
 {"memory_update","Save a user-approved note to ~/adam-ai/memory.jsonl.",PARAM1("note","Useful long-term note to retain")},
 {"task_manager","Add, finish or list tasks saved in ~/adam-ai/tasks.jsonl.","{\"type\":\"object\",\"required\":[\"action\"],\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"add\",\"done\",\"list\"]},\"text\":{\"type\":\"string\"},\"id\":{\"type\":\"string\"}}}"},
 {"web_search","Experimental text-only search engine query; may fail on anti-bot pages.",PARAM1("query","Internet search query")},
 {"web_extract","Extract readable text from a public HTTPS page.",PARAM1("url","Public HTTPS URL")},
 {"http_request","Read-only HTTP GET to a public HTTPS URL (no secrets or cookies).",PARAM1("url","Public HTTPS URL")},
 {"github_issues","Read public GitHub repository issues using the public API.",PARAM1("repo","owner/repo")},
 {"github_pull_request","Read public GitHub pull requests using the public API.",PARAM1("repo","owner/repo")},
 {"github_actions","Read public GitHub Actions runs using the public API.",PARAM1("repo","owner/repo")}

};

static void fail(char *dst,size_t cap,const char *fmt,...) {
 if(!dst||!cap)return;
 va_list ap;va_start(ap,fmt);vsnprintf(dst,cap,fmt,ap);va_end(ap);
}
static char *duplicate(const char *s){return s?strdup(s):NULL;}
static const char *arg_string(struct json_object *o,const char *key){
 struct json_object *v=NULL;
 if(!o||!json_object_object_get_ex(o,key,&v)||!json_object_is_type(v,json_type_string))return NULL;
 const char *text=json_object_get_string(v);
 return text && strlen(text)==(size_t)json_object_get_string_len(v) ? text : NULL;
}
static bool appendf(char **buf,size_t *len,size_t max,const char *fmt,...){
 if(*len>=max)return false;
 if(!*buf){*buf=calloc(max+1,1);if(!*buf)return false;}
 va_list ap;va_start(ap,fmt);int n=vsnprintf(*buf+*len,max-*len+1,fmt,ap);va_end(ap);
 if(n<0)return false;
 if((size_t)n>max-*len){*len=max;return false;}
 *len+=(size_t)n;return true;
}
struct json_object *adam_tools_schemas(void){
 struct json_object *a=json_object_new_array();if(!a)return NULL;
 for(size_t i=0;i<sizeof(specs)/sizeof(specs[0]);i++){
  const Spec *s=&specs[i];
  struct json_object *p=json_tokener_parse(s->parameters);
  struct json_object *fn=json_object_new_object(),*obj=json_object_new_object();
  if(!p||!fn||!obj){if(p)json_object_put(p);if(fn)json_object_put(fn);if(obj)json_object_put(obj);continue;}
  json_object_object_add(fn,"name",json_object_new_string(s->name));
  json_object_object_add(fn,"description",json_object_new_string(s->description));
  json_object_object_add(fn,"parameters",p);
  json_object_object_add(obj,"type",json_object_new_string("function"));
  json_object_object_add(obj,"function",fn);
  json_object_array_add(a,obj);
 }
 return a;
}
bool adam_tools_known(const char *name){
 if(!name)return false;
 for(size_t i=0;i<sizeof(specs)/sizeof(specs[0]);i++)if(!strcmp(specs[i].name,name))return true;
 return false;
}
bool adam_tools_is_network(const char *name){
 if(!name)return false;
 return !strcmp(name,"web_search") || !strcmp(name,"web_extract") ||
        !strcmp(name,"http_request") || !strcmp(name,"github_issues") ||
        !strcmp(name,"github_pull_request") || !strcmp(name,"github_actions");
}
static bool request(AdamToolContext *ctx,const char *description,char *error,size_t cap){
 if(!ctx->approve||!ctx->approve(description,ctx->approval_userdata)){
  fail(error,cap,"Operation denied by user");return false;
 }
 return true;
}

/* Safe workspace walk through directory fds. Every component is checked. */
static int parent_dir(AdamFs *fs,const char *path,char name[256]){
 if(!fs||fs->root_fd<0||!adam_fs_valid_path(path,false))return -1;
 int fd=dup(fs->root_fd);if(fd<0)return -1;
 const char *p=path;
 for(;;){
  const char *slash=strchr(p,'/');size_t n=slash?(size_t)(slash-p):strlen(p);
  if(n>255){close(fd);return -1;}
  memcpy(name,p,n);name[n]=0;
  if(!slash)return fd;
  int next=openat(fd,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  close(fd);if(next<0)return -1;fd=next;p=slash+1;
 }
}

/* Search is bounded in entries, recursion depth and output size. Secret files
 * are not opened and symlinks are never traversed. */
typedef struct{AdamFs *fs;const char *needle;bool names_only;char *out;size_t len;int seen,hits,files;} Search;
static void walk_search(Search *q,const char *dir,int depth){
 if(depth>SEARCH_DEPTH||q->seen>=512||q->hits>=SEARCH_HITS||q->len>=TOOL_REPLY_MAX-200)return;
 char error[180]={0},*listing=NULL;
 if(!adam_fs_list(q->fs,dir,&listing,error,sizeof(error)))return;
 char *save=NULL;
 for(char *line=strtok_r(listing,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
  if(line[0]!='['||strlen(line)<5||q->seen>=512||q->hits>=SEARCH_HITS)continue;
  char kind=line[1];const char *name=line+4;
  if(name[0]=='.' || !strcmp(name,"node_modules") || !strcmp(name,"build") ||
     !strcmp(name,"cmake-build-debug") || !strcmp(name,"out"))continue;
  size_t need=strlen(dir)+strlen(name)+3;
  if(need>1024)continue;
  char path[1100];snprintf(path,sizeof(path),"%s%s%s",!strcmp(dir,".")?"":dir,
                                   !strcmp(dir,".")?"":"/",name);
  if(!adam_fs_valid_path(path,false))continue;
  q->seen++;
  if(kind=='d'){if(depth<SEARCH_DEPTH)walk_search(q,path,depth+1);continue;}
  if(kind!='f')continue;
  q->files++;
  if(q->names_only){
   if(q->needle && strstr(name,q->needle)){
    appendf(&q->out,&q->len,TOOL_REPLY_MAX,"%s\n",path);q->hits++;
   }
   continue;
  }
  /* Prefer text-like source files; skip large media and artifacts. */
  const char *dot=strrchr(name,'.');
  if(!dot || !(strstr(".c .h .cpp .hpp .cc .md .txt .cmake .py .rs .json .sh .yml .yaml .toml .ini",dot)))continue;
  char *data=NULL;
  if(!adam_fs_read(q->fs,path,&data,error,sizeof(error)))continue;
  int lineno=1;char *start=data;
  for(char *p=data;;p++){
   if(*p!='\n' && *p!='\0')continue;
   char separator=*p;*p=0;
   if(strstr(start,q->needle)){
    appendf(&q->out,&q->len,TOOL_REPLY_MAX,"%s:%d: %.200s\n",path,lineno,start);
    q->hits++;
   }
   if(!separator||q->hits>=SEARCH_HITS||q->len>=TOOL_REPLY_MAX-300)break;
   start=p+1;lineno++;
  }
  free(data);
 }
 free(listing);
}
static bool search(AdamToolContext *ctx,const char *needle,bool names,char **out,
                   char *error,size_t cap){
 if(!needle||!needle[0]||strlen(needle)>128){fail(error,cap,"Search query must be 1..128 characters");return false;}
 Search state={.fs=ctx->fs,.needle=needle,.names_only=names};
 walk_search(&state,".",0);
 if(!state.out)state.out=duplicate("No matches (search is depth/size limited)");
 *out=state.out;return *out!=NULL;
}

/* argv-only command execution: no shell expansion, no inherited secrets.
 * USER APPROVAL IS ALWAYS REQUIRED for execute_command/build/ctest/commit.
 * This is intentionally NOT claimed to be a complete sandbox. */
static bool run_argv(AdamToolContext *ctx,const char *const *argv,int seconds,
                     char **output,char *error,size_t cap){
 if(!argv||!argv[0]||!argv[0][0]){fail(error,cap,"No executable provided");return false;}
 if(!ctx->allow_processes){fail(error,cap,"Process execution is disabled in this context");return false;}
 if(geteuid()==0){fail(error,cap,"Refusing to execute commands as root");return false;}
 const char *base=strrchr(argv[0],'/');base=base?base+1:argv[0];
 static const char *allow[]={"cmake","ninja","ctest","make","git","gcc","g++","cc","clang","clang++","clang-format","python3","rg","grep","ls","pwd","echo","ollama"};
 bool allowed=false;
 for(size_t i=0;i<sizeof(allow)/sizeof(*allow);++i)if(!strcmp(base,allow[i])&&!strchr(argv[0],'/'))allowed=true;
 if(!allowed){fail(error,cap,"Command not in safe executable allowlist");return false;}
 int pipes[2];if(pipe(pipes)!=0){fail(error,cap,"pipe failed");return false;}
 pid_t pid=fork();
 if(pid<0){close(pipes[0]);close(pipes[1]);fail(error,cap,"fork failed");return false;}
 if(pid==0){
  setpgid(0,0);
  close(pipes[0]);dup2(pipes[1],STDOUT_FILENO);dup2(pipes[1],STDERR_FILENO);close(pipes[1]);
  int null=open("/dev/null",O_RDONLY);if(null>=0){dup2(null,STDIN_FILENO);close(null);}
  if(chdir(ctx->fs->root)!=0)_exit(126);
  struct rlimit cpu={.rlim_cur=(rlim_t)(seconds+2),.rlim_max=(rlim_t)(seconds+3)};
  struct rlimit file={.rlim_cur=32*1024*1024,.rlim_max=32*1024*1024};
  struct rlimit nofiles={.rlim_cur=64,.rlim_max=64};
  struct rlimit core={.rlim_cur=0,.rlim_max=0};
  setrlimit(RLIMIT_CPU,&cpu);setrlimit(RLIMIT_FSIZE,&file);
  setrlimit(RLIMIT_NOFILE,&nofiles);setrlimit(RLIMIT_CORE,&core);
  /* Environment is minimal; do not inherit API keys, proxy settings or tokens. */
  clearenv();setenv("PATH","/usr/bin:/bin:/usr/local/bin",1);
  setenv("LANG","C.UTF-8",1);setenv("GIT_PAGER","cat",1);
  setenv("GIT_CONFIG_NOSYSTEM","1",1);setenv("GIT_CONFIG_GLOBAL","/dev/null",1);
  if(strcmp(base,"python3")==0)setenv("PYTHONNOUSERSITE","1",1);
  execvp(argv[0],(char *const*)argv);_exit(127);
 }
 close(pipes[1]);fcntl(pipes[0],F_SETFL,O_NONBLOCK);
 char *out=calloc(PROCESS_MAX_OUTPUT+1,1);
 if(!out){kill(-pid,SIGKILL);kill(pid,SIGKILL);waitpid(pid,NULL,0);close(pipes[0]);fail(error,cap,"Out of memory");return false;}
 size_t len=0;int status=0;bool finished=false,expired=false;time_t started=time(NULL);
 while(!finished){
  if(ctx->cancelled && atomic_load(ctx->cancelled)){expired=true;break;}
  if(time(NULL)-started>=seconds){expired=true;break;}
  struct pollfd pfd={pipes[0],POLLIN|POLLHUP,0};
  (void)poll(&pfd,1,100);
  if(pfd.revents&(POLLIN|POLLHUP)){
   char temp[4096];ssize_t n=read(pipes[0],temp,sizeof(temp));
   if(n>0){size_t copy=(size_t)n<PROCESS_MAX_OUTPUT-len?(size_t)n:PROCESS_MAX_OUTPUT-len;
    if(copy){memcpy(out+len,temp,copy);len+=copy;out[len]=0;}}
  }
  pid_t r=waitpid(pid,&status,WNOHANG);
  if(r==pid){finished=true;break;}
  if(r<0){expired=true;break;}
 }
 if(!finished){kill(-pid,SIGKILL);kill(pid,SIGKILL);waitpid(pid,&status,0);}
 /* Drain any buffered stdout/stderr after exit. */
 for(;;){char b[2048];ssize_t n=read(pipes[0],b,sizeof(b));if(n<=0)break;
  size_t copy=(size_t)n<PROCESS_MAX_OUTPUT-len?(size_t)n:PROCESS_MAX_OUTPUT-len;
  if(copy){memcpy(out+len,b,copy);len+=copy;out[len]=0;}}
 close(pipes[0]);
 char *full=malloc(len+160);if(!full){free(out);return false;}
 int code=WIFEXITED(status)?WEXITSTATUS(status):-1;
 snprintf(full,len+160,"Exit code: %d%s%s\n%s%s",code,expired?" (cancelled/timeout)":"",
          len>=PROCESS_MAX_OUTPUT?" [output truncated]":"",out,len?"":"(no output)");
 free(out);*output=full;
 if(expired){fail(error,cap,"Process cancelled or exceeded %d seconds",seconds);return false;}
 /* Nonzero exit is still a usable diagnostic result, not a tool failure. */
 return true;
}
static bool approved_run(AdamToolContext *ctx,const char *const *argv,bool requires_approval,
                         char **output,char *error,size_t cap){
 if(requires_approval){
  char desc[4096];size_t used=0;
  used+=(size_t)snprintf(desc,sizeof(desc),"TERMINAL COMMAND APPROVAL REQUIRED\n"
      "Working directory: %s\nCommand argv (no shell):\n",ctx->fs->root);
  for(size_t i=0;argv[i]&&i<PROCESS_MAX_ARGS&&used<sizeof(desc)-150;i++)
   used+=(size_t)snprintf(desc+used,sizeof(desc)-used,"[%zu] %s\n",i,argv[i]);
  snprintf(desc+used,sizeof(desc)-used,
      "Running build tools may execute project code. This is NOT a full sandbox.\n"
      "Press Y to execute this command, or N to reject.");
  if(!request(ctx,desc,error,cap))return false;
 }
 return run_argv(ctx,argv,30,output,error,cap);
}

static bool edit_file(AdamToolContext *ctx,const char *path,const char *oldtext,
                      const char *newtext,char **output,char *error,size_t cap){
 if(!path||!oldtext||!newtext||!*oldtext){fail(error,cap,"Missing path/old_text/new_text");return false;}
 char *src=NULL;if(!adam_fs_read(ctx->fs,path,&src,error,cap))return false;
 char *first=strstr(src,oldtext);
 if(!first){free(src);fail(error,cap,"old_text not found in file");return false;}
 if(strstr(first+strlen(oldtext),oldtext)){free(src);fail(error,cap,"old_text occurs more than once; make patch unique");return false;}
 size_t prefix=(size_t)(first-src),oldlen=strlen(oldtext),newlen=strlen(newtext);
 size_t size=strlen(src)-oldlen+newlen;
 if(size>ADAM_FS_WRITE_LIMIT){free(src);fail(error,cap,"Proposed file is over 32KiB write limit");return false;}
 char *proposal=malloc(size+1);if(!proposal){free(src);fail(error,cap,"Out of memory");return false;}
 memcpy(proposal,src,prefix);memcpy(proposal+prefix,newtext,newlen);
 strcpy(proposal+prefix+newlen,first+oldlen);free(src);
 size_t n=size+strlen(path)+320;
 char *preview=malloc(n);if(!preview){free(proposal);return false;}
 snprintf(preview,n,"EDIT/PATCH FILE APPROVAL REQUIRED\nTarget: %s\n"
          "This replaces one exact matching text region.\n"
          "Complete proposed file follows:\n--- BEGIN FILE ---\n%s\n--- END FILE ---",
          path,proposal);
 bool consent=request(ctx,preview,error,cap);free(preview);
 if(!consent){free(proposal);return false;}
 bool ok=adam_fs_write(ctx->fs,path,proposal,size,error,cap);
 free(proposal);
 if(ok)*output=duplicate("Approved exact-text edit written successfully");
 return ok && *output;
}

static bool file_mutation(AdamToolContext *ctx,const char *action,
                           const char *from,const char *to,
                           char **output,char *error,size_t cap){
 char name[256],target_name[256];int parent=parent_dir(ctx->fs,from,name);
 if(parent<0){fail(error,cap,"Unsafe or unavailable parent directory");return false;}
 int other=-1;
 if(to){other=parent_dir(ctx->fs,to,target_name);if(other<0){close(parent);fail(error,cap,"Unsafe or unavailable destination directory");return false;}}
 struct stat st;bool exists=fstatat(parent,name,&st,AT_SYMLINK_NOFOLLOW)==0;
 if(!strcmp(action,"create_directory")){
  if(exists){fail(error,cap,"Directory already exists");goto no;}
 } else if(!exists||!S_ISREG(st.st_mode)){
  fail(error,cap,"Source is not a regular file");goto no;
 }
 if(to&&fstatat(other,target_name,&st,AT_SYMLINK_NOFOLLOW)==0){fail(error,cap,"Destination exists; refusing overwrite");goto no;}
 char confirm[2300];snprintf(confirm,sizeof(confirm),
      "FILE OPERATION APPROVAL REQUIRED\nAction: %s\nSource: %s\nDestination: %s\n"
      "This may remove or move project data. Press Y to allow.",
      action,from,to?to:"(none)");
 if(!request(ctx,confirm,error,cap))goto no;
 bool ok=false;
 if(!strcmp(action,"create_directory"))ok=mkdirat(parent,name,0755)==0;
 else if(!strcmp(action,"delete_file")){
  struct stat check;
  if(fstatat(parent,name,&check,AT_SYMLINK_NOFOLLOW)==0 && S_ISREG(check.st_mode))
   ok=unlinkat(parent,name,0)==0;
 } else if(!strcmp(action,"move_file")){
  /* Linux-only RENAME_NOREPLACE prevents overwriting under a race. */
#ifdef __linux__
  ok=renameat2(parent,name,other,target_name,RENAME_NOREPLACE)==0;
#endif
 }
 if(!ok)fail(error,cap,"Filesystem operation failed: %s",strerror(errno));
 close(parent);if(other>=0)close(other);
 if(ok)*output=duplicate("User-approved filesystem operation completed");
 return ok && *output;
no:
 close(parent);if(other>=0)close(other);return false;
}

static bool hardware(char **out){
 size_t len=0;appendf(out,&len,6000,"AdamAI hardware summary (local /proc):\n");
 FILE *f=fopen("/proc/cpuinfo","r");char line[512];
 if(f){while(fgets(line,sizeof(line),f))if(!strncmp(line,"model name",10)){
  appendf(out,&len,6000,"CPU: %.250s",line);break;}fclose(f);}
 f=fopen("/proc/meminfo","r");
 if(f){while(fgets(line,sizeof(line),f))if(!strncmp(line,"MemTotal:",9)||!strncmp(line,"MemAvailable:",13))appendf(out,&len,6000,"%.120s",line);fclose(f);}
 return *out!=NULL;
}

/* User notes and simple task log stay on local disk, outside repositories.
 * File permissions are private and mutations require approval. */
static int private_store(const char *filename,bool create,char *error,size_t cap) {
 const char *home=getenv("HOME");
 if(!home||home[0]!='/'||strlen(home)>1000){fail(error,cap,"HOME not configured");return -1;}
 char folder[1300];snprintf(folder,sizeof(folder),"%s/adam-ai",home);
 if(create && mkdir(folder,0700)!=0 && errno!=EEXIST){fail(error,cap,"Cannot create notes folder");return -1;}
 int d=open(folder,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(d<0){if(!create && errno==ENOENT)return -2;fail(error,cap,"Cannot open private AdamAI directory");return -1;}
 struct stat st;if(fstat(d,&st)!=0||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()){
  close(d);fail(error,cap,"Unsafe owner for AdamAI note directory");return -1;
 }
 int flags=(create?O_RDWR|O_CREAT:O_RDONLY)|O_CLOEXEC|O_NOFOLLOW;
 int fd=openat(d,filename,flags,0600);close(d);
 if(fd<0){if(!create && errno==ENOENT)return -2;fail(error,cap,"Cannot open private note file");return -1;}
 if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=getuid()){
  close(fd);fail(error,cap,"Unsafe ownership/type of notes file");return -1;
 }
 if(st.st_mode&0077){
  if(!create||fchmod(fd,0600)!=0){close(fd);fail(error,cap,"Private notes file permissions are too open");return -1;}
 }
 return fd;
}
static bool append_event(int fd,struct json_object *event,char *error,size_t cap){
 if(flock(fd,LOCK_EX)!=0){fail(error,cap,"Cannot lock note file");return false;}
 struct stat st;
 if(fstat(fd,&st)!=0||st.st_size>1024*1024){flock(fd,LOCK_UN);fail(error,cap,"Private data store full");return false;}
 const char *text=json_object_to_json_string_ext(event,JSON_C_TO_STRING_PLAIN);
 size_t n=text?strlen(text):0;
 if(!n||lseek(fd,0,SEEK_END)<0){flock(fd,LOCK_UN);return false;}
 bool ok=true;
 if(write(fd,text,n)!=(ssize_t)n||write(fd,"\n",1)!=1||fsync(fd)!=0)ok=false;
 flock(fd,LOCK_UN);
 if(!ok)fail(error,cap,"Failed to save local note");
 return ok;
}
static char *read_store(int fd){
 struct stat st;if(fstat(fd,&st)!=0||st.st_size<0||st.st_size>1024*1024)return NULL;
 char *buf=calloc((size_t)st.st_size+1,1);
 if(!buf)return NULL;
 size_t used=0;
 while(used<(size_t)st.st_size){ssize_t n=pread(fd,buf+used,(size_t)st.st_size-used,(off_t)used);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)break;
  used+=(size_t)n;
 }
 buf[used]=0;return buf;
}
static bool memory_op(AdamToolContext *ctx,const char *name,struct json_object *args,
                      char **output,char *error,size_t cap){
 bool writing=!strcmp(name,"memory_update");
 const char *value=arg_string(args,writing?"note":"query");
 if(!value||!*value||strlen(value)>1024){fail(error,cap,"Note/query must be 1..1024 bytes");return false;}
 if(writing){
  char preview[1400];snprintf(preview,sizeof(preview),
    "LONG-TERM MEMORY APPROVAL REQUIRED\nAdamAI wants to remember:\n%s\nPress Y to save the note.",value);
  if(!request(ctx,preview,error,cap))return false;
 }
 int fd=private_store("memory.jsonl",writing,error,cap);
 if(fd==-2){*output=duplicate("No saved memories");return *output!=NULL;}
 if(fd<0)return false;
 if(writing){
  struct json_object *obj=json_object_new_object();
  json_object_object_add(obj,"note",json_object_new_string(value));
  bool ok=append_event(fd,obj,error,cap);json_object_put(obj);close(fd);
  if(ok)*output=duplicate("Approved note saved in local AdamAI memory");
  return ok&&*output;
 }
 char *data=read_store(fd);close(fd);
 if(!data){fail(error,cap,"Could not read notes");return false;}
 size_t len=0;int hits=0;char *cursor=NULL;
 for(char *line=strtok_r(data,"\n",&cursor);line&&hits<24;line=strtok_r(NULL,"\n",&cursor)){
  struct json_object *obj=json_tokener_parse(line),*v=NULL;
  if(obj&&json_object_object_get_ex(obj,"note",&v)&&json_object_is_type(v,json_type_string)){
   const char *note=json_object_get_string(v);
   if(strcasestr(note,value)){appendf(output,&len,10000,"- %.1000s\n",note);hits++;}
  }
  if(obj)json_object_put(obj);
 }
 free(data);
 if(!*output)*output=duplicate("No matching memories");
 return *output!=NULL;
}
typedef struct{char id[64],text[1024];bool done;} SavedTask;
static bool task_op(AdamToolContext *ctx,struct json_object *args,
                    char **output,char *error,size_t cap){
 const char *action=arg_string(args,"action");
 if(!action|| (strcmp(action,"list")&&strcmp(action,"add")&&strcmp(action,"done"))){
  fail(error,cap,"task_manager action must be add, done, list");return false;
 }
 const char *text=arg_string(args,"text"),*id=arg_string(args,"id");
 bool writing=strcmp(action,"list")!=0;
 if(writing){
  if(!strcmp(action,"add")&&(!text||!*text||strlen(text)>=1024)){
   fail(error,cap,"Task text must be 1..1023 bytes");return false;
  }
  if(!strcmp(action,"done")&&(!id||!id[0]||strlen(id)>63)){
   fail(error,cap,"Task ID required");return false;
  }
  char preview[1400];snprintf(preview,sizeof(preview),
    "TASK CHANGE APPROVAL REQUIRED\nAction: %s\nTask: %.1000s\n"
    "ID: %.60s\nPress Y to approve task update.",action,text?text:"",id?id:"");
  if(!request(ctx,preview,error,cap))return false;
 }
 int fd=private_store("tasks.jsonl",writing,error,cap);
 if(fd==-2){*output=duplicate("No tasks saved");return *output!=NULL;}
 if(fd<0)return false;
 if(writing){
  struct json_object *e=json_object_new_object();
  json_object_object_add(e,"action",json_object_new_string(action));
  char generated[64];
  if(!strcmp(action,"add")){
   struct timespec now;clock_gettime(CLOCK_REALTIME,&now);
   snprintf(generated,sizeof(generated),"%ld-%09ld-%ld",(long)now.tv_sec,now.tv_nsec,(long)getpid());
   json_object_object_add(e,"id",json_object_new_string(generated));
   json_object_object_add(e,"text",json_object_new_string(text));
  } else json_object_object_add(e,"id",json_object_new_string(id));
  bool ok=append_event(fd,e,error,cap);json_object_put(e);close(fd);
  if(ok){char msg[120];snprintf(msg,sizeof(msg),"Task updated. ID: %s",!strcmp(action,"add")?generated:id);
   *output=duplicate(msg);}
  return ok&&*output;
 }
 char *data=read_store(fd);close(fd);
 if(!data){fail(error,cap,"Could not read tasks");return false;}
 SavedTask tasks[256]={0};size_t count=0;
 char *cursor=NULL;
 for(char *line=strtok_r(data,"\n",&cursor);line;line=strtok_r(NULL,"\n",&cursor)){
  struct json_object *obj=json_tokener_parse(line);
  if(!obj)continue;
  struct json_object *a=NULL,*tid=NULL,*t=NULL;
  const char *act=json_object_object_get_ex(obj,"action",&a)?json_object_get_string(a):NULL;
  const char *taskid=json_object_object_get_ex(obj,"id",&tid)?json_object_get_string(tid):NULL;
  const char *body=json_object_object_get_ex(obj,"text",&t)?json_object_get_string(t):NULL;
  if(act&&taskid){
   if(!strcmp(act,"add")&&body&&count<256){
    snprintf(tasks[count].id,sizeof(tasks[count].id),"%s",taskid);
    snprintf(tasks[count].text,sizeof(tasks[count].text),"%s",body);count++;
   } else if(!strcmp(act,"done"))for(size_t i=0;i<count;i++)if(!strcmp(tasks[i].id,taskid))tasks[i].done=true;
  }
  json_object_put(obj);
 }
 free(data);
 size_t len=0;for(size_t i=0;i<count;i++){
  if(!appendf(output,&len,10000,"[%c] %s: %.400s\n",tasks[i].done?'x':' ',tasks[i].id,tasks[i].text))break;
 }
 if(!*output)*output=duplicate("No tasks saved");
 return *output!=NULL;
}

/* Read-only public network helpers. Every full URL is shown to the user
 * before sending when filesystem access is active. */
static bool fetch_public(AdamToolContext *ctx,const char *url,
                         char **output,char *error,size_t cap){
 if(!ctx->allow_network){fail(error,cap,"Web access disabled");return false;}
 if(!adam_web_validate_url(url,error,cap))return false;
 size_t n=strlen(url)+512;char *prompt=malloc(n);
 if(!prompt){fail(error,cap,"Out of memory");return false;}
 snprintf(prompt,n,"OUTBOUND WEB REQUEST APPROVAL REQUIRED\nDestination URL:\n%s\n"
         "Inspect FULL URL for personal or project data before allowing.\n"
         "Press Y to make this public HTTPS request.",url);
 const char *confirm=getenv("ADAM_WEB_CONFIRM");
 bool ask=confirm && (!strcmp(confirm,"1") ||
                       !strcmp(confirm,"true") || !strcmp(confirm,"on"));
 bool yes=!ask || request(ctx,prompt,error,cap);
 free(prompt);
 if(!yes)return false;
 AdamWebPage page={0};
 bool ok=adam_web_fetch(url,ctx->cancelled,&page,error,cap);
 if(ok){
  const char *actual=page.final_url ? page.final_url : url;
  size_t need=strlen(actual)+strlen(page.text)+256;
  *output=malloc(need);
  if(*output)snprintf(*output,need,"Source URL: %s\nUntrusted public web content:\n%s%s",
                       actual,page.text,page.truncated?"\n[truncated]":"");
  else ok=false;
 }
 adam_web_page_free(&page);
 return ok&&*output;
}
static bool good_repo(const char *repo){
 if(!repo||strlen(repo)>150)return false;
 const char *slash=strchr(repo,'/');
 if(!slash||slash==repo||slash[1]==0||strchr(slash+1,'/'))return false;
 for(const char *p=repo;*p;p++){
  unsigned char c=(unsigned char)*p;
  if(!(isalnum(c)||c=='.'||c=='_'||c=='-'||c=='/'))return false;
 }
 return true;
}
static bool network_op(AdamToolContext *ctx,const char *name,struct json_object *args,
                       char **output,char *error,size_t cap){
 const char *url=arg_string(args,"url");
 if(!strcmp(name,"web_extract")||!strcmp(name,"http_request")){
  if(!url){fail(error,cap,"Missing HTTPS URL");return false;}
  return fetch_public(ctx,url,output,error,cap);
 }
 if(!strcmp(name,"web_search")){
  const char *q=arg_string(args,"query");
  if(!q||!*q||strlen(q)>240){fail(error,cap,"Query must be 1..240 bytes");return false;}
  CURL *curl=curl_easy_init();if(!curl){fail(error,cap,"libcurl unavailable");return false;}
  char *encoded=curl_easy_escape(curl,q,0);
  if(!encoded){curl_easy_cleanup(curl);return false;}
  size_t size=strlen(encoded)+100;char *dest=malloc(size);
  if(dest)snprintf(dest,size,"https://html.duckduckgo.com/html/?q=%s",encoded);
  curl_free(encoded);curl_easy_cleanup(curl);
  if(!dest)return false;
  bool ok=fetch_public(ctx,dest,output,error,cap);free(dest);
  return ok;
 }
 const char *repo=arg_string(args,"repo");
 if(!good_repo(repo)){fail(error,cap,"Repository must be owner/repo");return false;}
 const char *end=!strcmp(name,"github_issues")?"issues?state=open&per_page=10":
                 !strcmp(name,"github_pull_request")?"pulls?state=open&per_page=10":
                 "actions/runs?per_page=5";
 char addr[512];snprintf(addr,sizeof(addr),"https://api.github.com/repos/%s/%s",repo,end);
 return fetch_public(ctx,addr,output,error,cap);
}

bool adam_tools_execute(const char *name,struct json_object *args,AdamToolContext *ctx,
                        char **output,char *error,size_t cap){
 if(output)*output=NULL;
 if(!name||!ctx||!ctx->fs||!output){fail(error,cap,"Invalid tool context");return false;}
 if(ctx->cancelled && atomic_load(ctx->cancelled)){fail(error,cap,"Cancelled");return false;}
 if(!strcmp(name,"find_files"))return search(ctx,arg_string(args,"query"),true,output,error,cap);
 if(!strcmp(name,"search_files")||!strcmp(name,"find_symbol")||!strcmp(name,"find_references")){
  const char *key=!strcmp(name,"search_files")?"query":"symbol";
  return search(ctx,arg_string(args,key),false,output,error,cap);
 }
 if(!strcmp(name,"project_index")){
  Search q={.fs=ctx->fs,.needle=NULL,.names_only=true};walk_search(&q,".",0);
  char text[256];snprintf(text,sizeof(text),"Indexed/scanned %d visible files among %d entries (max depth 4, 512 entries).",q.files,q.seen);
  free(q.out);*output=duplicate(text);return *output!=NULL;
 }
 if(!strcmp(name,"inspect_dependencies")){
  if(adam_fs_read(ctx->fs,"CMakeLists.txt",output,error,cap))return true;
  fail(error,cap,"No readable CMakeLists.txt in workspace");return false;
 }
 if(!strcmp(name,"edit_file")||!strcmp(name,"apply_patch"))
  return edit_file(ctx,arg_string(args,"path"),arg_string(args,"old_text"),arg_string(args,"new_text"),output,error,cap);
 if(!strcmp(name,"create_directory"))return file_mutation(ctx,name,arg_string(args,"path"),NULL,output,error,cap);
 if(!strcmp(name,"delete_file"))return file_mutation(ctx,name,arg_string(args,"path"),NULL,output,error,cap);
 if(!strcmp(name,"move_file"))return file_mutation(ctx,name,arg_string(args,"from"),arg_string(args,"to"),output,error,cap);
 if(!strcmp(name,"hardware_info"))return hardware(output);
 if(!strcmp(name,"execute_command")){
  struct json_object *arr=NULL;
  if(!args||!json_object_object_get_ex(args,"argv",&arr)||!json_object_is_type(arr,json_type_array) ||
     json_object_array_length(arr)==0 || json_object_array_length(arr)>PROCESS_MAX_ARGS){
   fail(error,cap,"argv must be nonempty array of at most 24 strings");return false;
  }
  const char *argv[PROCESS_MAX_ARGS+1]={0};size_t n=json_object_array_length(arr);
  for(size_t i=0;i<n;++i){
   struct json_object *x=json_object_array_get_idx(arr,i);
   if(!json_object_is_type(x,json_type_string)||json_object_get_string_len(x)>512){
    fail(error,cap,"Argument invalid or >512 bytes");return false;
   }
   argv[i]=json_object_get_string(x);
   if(!argv[i]||strchr(argv[i],'\n')){fail(error,cap,"Bad argument");return false;}
  }
  return approved_run(ctx,argv,true,output,error,cap);
 }
 if(!strcmp(name,"build_project")||!strcmp(name,"get_diagnostics")){
  const char *argv[]={"cmake","--build","build","-j2",NULL};
  return approved_run(ctx,argv,true,output,error,cap);
 }
 if(!strcmp(name,"run_tests")){
  const char *argv[]={"ctest","--test-dir","build","--output-on-failure",NULL};
  return approved_run(ctx,argv,true,output,error,cap);
 }
 if(!strcmp(name,"git_status")){
  const char *argv[]={"git","--no-pager","status","--short",NULL};
  return approved_run(ctx,argv,false,output,error,cap);
 }
 if(!strcmp(name,"git_diff")){
  const char *argv[]={"git","--no-pager","diff","--no-ext-diff",NULL};
  return approved_run(ctx,argv,false,output,error,cap);
 }
 if(!strcmp(name,"git_log")){
  const char *argv[]={"git","--no-pager","log","-8","--oneline",NULL};
  return approved_run(ctx,argv,false,output,error,cap);
 }
 if(!strcmp(name,"git_branch")){
  const char *argv[]={"git","--no-pager","branch","--list",NULL};
  return approved_run(ctx,argv,false,output,error,cap);
 }
 if(!strcmp(name,"git_commit")){
  const char *m=arg_string(args,"message");
  if(!m||!*m||strlen(m)>200){fail(error,cap,"Commit message is required (1..200 bytes)");return false;}
  const char *argv[]={"git","-c","user.name=AdamAI","-c","user.email=adamai@localhost","commit","-m",m,NULL};
  return approved_run(ctx,argv,true,output,error,cap);
 }
 if(!strcmp(name,"model_manager")){
  const char *argv[]={"ollama","list",NULL};
  return approved_run(ctx,argv,false,output,error,cap);
 }
 if(!strcmp(name,"format_code")){
  const char *path=arg_string(args,"path");
  if(!path||!adam_fs_valid_path(path,false)){fail(error,cap,"Invalid format target");return false;}
  /* Check the file through the scoped FS layer before invoking any external
   * formatter. Run it only after user permission (not a kernel sandbox). */
  char *source=NULL;
  if(!adam_fs_read(ctx->fs,path,&source,error,cap))return false;
  free(source);
  const char *argv[]={"clang-format","--style=file",path,NULL};
  return approved_run(ctx,argv,true,output,error,cap);
 }
 if(!strcmp(name,"memory_search")||!strcmp(name,"memory_update"))
  return memory_op(ctx,name,args,output,error,cap);
 if(!strcmp(name,"task_manager"))
  return task_op(ctx,args,output,error,cap);
 if(!strcmp(name,"web_search")||!strcmp(name,"web_extract")||
    !strcmp(name,"http_request")||!strcmp(name,"github_issues")||
    !strcmp(name,"github_pull_request")||!strcmp(name,"github_actions"))
  return network_op(ctx,name,args,output,error,cap);
 fail(error,cap,"Unknown tool: %s",name);return false;
}

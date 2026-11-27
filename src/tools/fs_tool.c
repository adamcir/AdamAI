/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "adam/fs_tool.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#error "AdamAI filesystem sandbox requires O_NOFOLLOW"
#endif

static void err(char *s, size_t n, const char *fmt, ...) {
    if (!s || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, n, fmt, ap);
    va_end(ap);
}

bool adam_fs_valid_path(const char *path, bool allow_dot) {
    if (!path || !*path) return false;
    if (allow_dot && strcmp(path, ".") == 0) return true;
    if (path[0] == '/' || path[0] == '~' || strlen(path) > 1024) return false;
    const char *p = path;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 0 || len > 255 || (len == 1 && p[0] == '.') ||
            (len == 2 && p[0] == '.' && p[1] == '.')) return false;
        /* Common project secrets and VCS internals are never exposed to the model. */
        if ((len == 4 && !strncmp(p,".git",4)) ||
            (len >= 4 && !strncmp(p,".env",4)) ||
            (len == 4 && !strncmp(p,".ssh",4)) ||
            (len == 4 && !strncmp(p,".aws",4)) ||
            (len == 7 && !strncmp(p,"secrets",7)) ||
            (len == 6 && !strncmp(p,"id_rsa",6))) return false;
        for (size_t i = 0; i < len; ++i)
            if ((unsigned char)p[i] < 32 || (unsigned char)p[i] == 127) return false;
        if (!slash) break;
        p = slash + 1;
    }
    return true;
}

bool adam_fs_init(AdamFs *fs, const char *root, char *error, size_t error_cap) {
    if (!fs || !root) return false;
    memset(fs, 0, sizeof(*fs));
    fs->root_fd = -1;
    char *real = realpath(root, NULL);
    if (!real) { err(error, error_cap, "Cannot resolve workspace: %s", strerror(errno)); return false; }
    if (strlen(real) >= sizeof(fs->root)) {
        free(real); err(error, error_cap, "Workspace path too long"); return false;
    }
    int fd = open(real, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) { err(error, error_cap, "Cannot open workspace: %s", strerror(errno)); free(real); return false; }
    fs->root_fd = fd;
    memcpy(fs->root, real, strlen(real) + 1);
    free(real);
    return true;
}
void adam_fs_close(AdamFs *fs) {
    if (!fs) return;
    if (fs->root_fd >= 0) close(fs->root_fd);
    fs->root_fd = -1;
}

/* Open the parent using *only* directory fds, with no symlink following. */
static int parent_fd(AdamFs *fs, const char *path, char name[256],
                     char *error, size_t cap) {
    if (!fs || fs->root_fd < 0 || !adam_fs_valid_path(path, false)) {
        err(error, cap, "Invalid workspace-relative path (no absolute paths, .. or symlinks)");
        return -1;
    }
    int fd = dup(fs->root_fd);
    if (fd < 0) { err(error,cap,"Cannot access workspace"); return -1; }
    const char *p = path;
    while (true) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        char component[256];
        memcpy(component, p, len);
        component[len] = '\0';
        if (!slash) { memcpy(name, component, len + 1); return fd; }
        int next = openat(fd, component, O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW);
        if (next < 0) {
            err(error, cap, "Cannot enter directory %s: %s", component, strerror(errno));
            close(fd); return -1;
        }
        close(fd);
        fd = next;
        p = slash + 1;
    }
}

static bool open_read(AdamFs *fs, const char *path, int *outfd,
                      struct stat *st, char *error, size_t cap) {
    char name[256];
    int dir = parent_fd(fs, path, name, error, cap);
    if (dir < 0) return false;
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    int saved = errno;
    close(dir);
    if (fd < 0) { err(error, cap, "Cannot read %s: %s", path, strerror(saved)); return false; }
    if (fstat(fd, st) != 0 || !S_ISREG(st->st_mode)) {
        err(error, cap, "Not a regular file: %s", path); close(fd); return false;
    }
    *outfd = fd;
    return true;
}

/* Reject binary data/invalid UTF-8 before passing source text to a model. */
static bool utf8_text(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned char c = s[i];
        if (c == 0 || (c < 32 && c != '\n' && c != '\t' && c != '\r') || c == 127) return false;
        if (c < 0x80) { ++i; continue; }
        unsigned need = c>=0xF0 && c<=0xF4 ? 3 : c>=0xE0 && c<=0xEF ? 2 : c>=0xC2 && c<=0xDF ? 1 : 0;
        if (!need || i+need>=n) return false;
        uint32_t cp = c & (need==1 ? 0x1F : need==2 ? 0x0F : 0x07);
        for (unsigned j=1; j<=need; ++j) {
            if ((s[i+j]&0xC0)!=0x80) return false;
            cp=(cp<<6)|(s[i+j]&0x3F);
        }
        if (cp < (need==1 ? 0x80u : need==2 ? 0x800u : 0x10000u) ||
            cp > 0x10FFFF || (cp>=0xD800 && cp<=0xDFFF)) return false;
        i+=need+1;
    }
    return true;
}

bool adam_fs_read(AdamFs *fs, const char *path, char **output,
                  char *error, size_t cap) {
    if (!output) return false;
    *output = NULL;
    struct stat st; int fd;
    if (!open_read(fs,path,&fd,&st,error,cap)) return false;
    if (st.st_size > ADAM_FS_READ_LIMIT || st.st_size < 0) {
        err(error,cap,"File too large (limit %u bytes)",ADAM_FS_READ_LIMIT); close(fd); return false;
    }
    size_t max = ADAM_FS_READ_LIMIT;
    char *buf=malloc(max+2);
    if (!buf) {close(fd);err(error,cap,"Out of memory");return false;}
    size_t n=0; bool failed=false;
    while (n<=max) {
        ssize_t got=read(fd,buf+n,max+1-n);
        if (got<0 && errno==EINTR) continue;
        if (got<0) {err(error,cap,"Read failed: %s",strerror(errno));failed=true;break;}
        if (got==0) break;
        n+=(size_t)got;
    }
    close(fd);
    if (failed) {free(buf);return false;}
    if (n>max) {err(error,cap,"File too large");free(buf);return false;}
    if (!utf8_text((const unsigned char*)buf,n)) {
        err(error,cap,"Not a UTF-8 text file");free(buf);return false;
    }
    buf[n]=0;
    *output=buf;
    return true;
}

typedef struct {char *name; char kind;} Entry;
static int cmp_entry(const void *a,const void *b) {
    const Entry *x=a,*y=b;return strcmp(x->name,y->name);
}
bool adam_fs_list(AdamFs *fs, const char *path, char **output,
                  char *error, size_t cap) {
    if (!output || !fs || fs->root_fd<0) return false;
    *output=NULL;
    if (!path || !*path) path=".";
    int fd=-1;
    if (!strcmp(path,".")) fd=openat(fs->root_fd,".",O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW);
    else {
        char name[256]; int parent=parent_fd(fs,path,name,error,cap);
        if (parent<0)return false;
        fd=openat(parent,name,O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW);
        close(parent);
    }
    if (fd<0) {err(error,cap,"Cannot open directory: %s",strerror(errno));return false;}
    DIR *dir=fdopendir(fd);
    if (!dir) {err(error,cap,"Cannot list directory");close(fd);return false;}
    Entry entries[ADAM_FS_LIST_LIMIT]={0};size_t count=0;bool more=false;
    struct dirent *entry;
    while ((entry=readdir(dir))) {
        if (!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        /* Never reveal restricted names to the model. */
        if (!adam_fs_valid_path(entry->d_name,false))continue;
        if (count>=ADAM_FS_LIST_LIMIT) {more=true;break;}
        struct stat st;
        char kind='?';
        if (fstatat(fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)==0) {
            kind=S_ISDIR(st.st_mode)?'d':S_ISREG(st.st_mode)?'f':S_ISLNK(st.st_mode)?'l':'?';
        }
        entries[count].name=strdup(entry->d_name);
        if(!entries[count].name)break;
        entries[count++].kind=kind;
    }
    closedir(dir);
    qsort(entries,count,sizeof(*entries),cmp_entry);
    size_t needed=256;
    for(size_t i=0;i<count;i++)needed+=strlen(entries[i].name)+12;
    char *buf=calloc(needed,1);
    if(!buf){for(size_t i=0;i<count;i++)free(entries[i].name);err(error,cap,"Out of memory");return false;}
    int k=snprintf(buf,needed,"Listing %s relative to workspace:\n",path);
    size_t at=k>0?(size_t)k:0;
    for(size_t i=0;i<count;i++) {
        at+=(size_t)snprintf(buf+at,needed-at,"[%c] %s\n",entries[i].kind,entries[i].name);
        free(entries[i].name);
    }
    if(more)snprintf(buf+at,needed-at,"[More entries omitted]\n");
    *output=buf;
    return true;
}

bool adam_fs_target_info(AdamFs *fs, const char *path, bool *exists,
                         size_t *size, char *error, size_t cap) {
    if (!exists||!size) return false;
    *exists=false;*size=0;
    char name[256];int dir=parent_fd(fs,path,name,error,cap);
    if(dir<0)return false;
    struct stat st;
    if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)==0){
        if(!S_ISREG(st.st_mode)){err(error,cap,"Target must be a regular file (no symlinks)");close(dir);return false;}
        *exists=true;*size=st.st_size>=0?(size_t)st.st_size:0;
    } else if(errno!=ENOENT){err(error,cap,"Cannot inspect target: %s",strerror(errno));close(dir);return false;}
    close(dir);return true;
}

bool adam_fs_write(AdamFs *fs, const char *path, const char *content,
                   size_t length, char *error, size_t cap) {
    if (!content||length>ADAM_FS_WRITE_LIMIT||
        !utf8_text((const unsigned char*)content,length)){
        err(error,cap,"Write rejected: size, UTF-8 or binary content");return false;
    }
    char name[256];int dir=parent_fd(fs,path,name,error,cap);
    if(dir<0)return false;
    struct stat st;mode_t mode=0600;
    if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)==0){
        if(!S_ISREG(st.st_mode)){err(error,cap,"Refusing to overwrite symlink/non-file");close(dir);return false;}
        mode=st.st_mode&0777;
    } else if(errno!=ENOENT){err(error,cap,"Cannot inspect target: %s",strerror(errno));close(dir);return false;}
    char tmp[80];int fd=-1;
    for(unsigned i=0;i<16;++i){
        snprintf(tmp,sizeof(tmp),".adamai-%ld-%u.tmp",(long)getpid(),i);
        fd=openat(dir,tmp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd>=0)break;
        if(errno!=EEXIST)break;
    }
    if(fd<0){err(error,cap,"Cannot create temporary file: %s",strerror(errno));close(dir);return false;}
    bool ok=fchmod(fd,mode)==0;
    size_t done=0;
    while(ok&&done<length){
        ssize_t w=write(fd,content+done,length-done);
        if(w<0&&errno==EINTR)continue;
        if(w<=0){ok=false;break;}
        done+=(size_t)w;
    }
    if(ok&&fsync(fd)!=0)ok=false;
    if(close(fd)!=0)ok=false;
    if(ok){
        /* Reject a late-created symlink or special target before rename. */
        struct stat check;
        if(fstatat(dir,name,&check,AT_SYMLINK_NOFOLLOW)==0) {
            if(!S_ISREG(check.st_mode))ok=false;
        } else if(errno!=ENOENT)ok=false;
    }
    if(ok&&renameat(dir,tmp,dir,name)!=0)ok=false;
    if(ok)(void)fsync(dir);
    if(!ok){err(error,cap,"Write failed: %s",strerror(errno));unlinkat(dir,tmp,0);}
    close(dir);return ok;
}

#define _GNU_SOURCE
#include <spawn.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
extern char **environ;
int bv_fa_init(void **p){ *p = malloc(sizeof(posix_spawn_file_actions_t)); return posix_spawn_file_actions_init(*p); }
int bv_fa_destroy(void **p){ int r = posix_spawn_file_actions_destroy(*p); free(*p); return r; }
int bv_fa_dup2(void **p,int a,int b){ return posix_spawn_file_actions_adddup2(*p,a,b); }
int bv_fa_close(void **p,int a){ return posix_spawn_file_actions_addclose(*p,a); }
int bv_fa_open(void **p,int fd,const char*path,int fl,unsigned short m){ return posix_spawn_file_actions_addopen(*p,fd,path,fl,m); }
int bv_spawn(pid_t*pid,const char*path,void**fa,const void*attr,char*const argv[],char*const envp[]){ return posix_spawn(pid,path,*fa,attr,argv,envp); }
char ***_NSGetEnviron(void){ return &environ; }
int *__error(void){ return __errno_location(); }
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
void os_unfair_lock_lock(uint32_t*l){ (void)l; pthread_mutex_lock(&mu); }
void os_unfair_lock_unlock(uint32_t*l){ (void)l; pthread_mutex_unlock(&mu); }
int _NSGetExecutablePath(char*b,uint32_t*s){ (void)b;(void)s; return -1; }
int bv_test_main(void);
int main(void){ return bv_test_main(); }

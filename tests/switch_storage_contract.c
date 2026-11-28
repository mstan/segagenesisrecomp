/* Host check of settings/save replacement against the Switch filesystem's
 * non-overwriting rename contract. Run in an empty disposable directory. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int __wrap_rename(const char *,const char *);
FILE *__wrap_fopen(const char *,const char *);
static int calls,fail_call,fail_rollback;

FILE *__real_fopen(const char *path,const char *mode){return fopen(path,mode);}
int __real_rename(const char *from,const char *to)
{
    struct stat info;
    ++calls;
    if(calls==fail_call || (fail_rollback && strstr(from,".genesis-replace-backup"))) {
        errno=EIO;return -1;
    }
    if(!stat(to,&info)){errno=EEXIST;return -1;}
    return rename(from,to);
}
static void put(const char *path,const char *value)
{
    FILE *f=fopen(path,"wb");assert(f);
    assert(fputs(value,f)>=0);assert(fclose(f)==0);
}
static void expect(const char *path,const char *value)
{
    char buffer[64]={0};FILE *f=__wrap_fopen(path,"rb");assert(f);
    size_t size=fread(buffer,1,sizeof(buffer)-1,f);assert(!ferror(f));
    assert(fclose(f)==0);assert(size==strlen(value));assert(!strcmp(buffer,value));
}
static void missing(const char *path){struct stat info;assert(stat(path,&info)<0 && errno==ENOENT);}
int main(void)
{
    /* First save, followed by replacement of an existing save. */
    put("new.tmp","first");assert(__wrap_rename("new.tmp","settings.ini")==0);
    put("new.tmp","second");assert(__wrap_rename("new.tmp","settings.ini")==0);
    expect("settings.ini","second");missing("settings.ini.genesis-replace-backup");
    missing("new.tmp");
    /* A failed second move restores the original, with the new input intact. */
    put("new.tmp","third");calls=0;fail_call=3;
    assert(__wrap_rename("new.tmp","settings.ini")<0 && errno==EIO);
    expect("settings.ini","second");expect("new.tmp","third");
    missing("settings.ini.genesis-replace-backup");fail_call=0;
    /* A failed rollback retains the only original copy for the next open. */
    calls=0;fail_call=3;fail_rollback=1;
    assert(__wrap_rename("new.tmp","settings.ini")<0);
    missing("settings.ini");fail_call=0;
    /* A failed recovery must not let write mode create an empty replacement. */
    assert(__wrap_fopen("settings.ini","wb")==NULL);
    missing("settings.ini");
    expect("settings.ini.genesis-replace-backup","second");fail_rollback=0;
    expect("settings.ini","second");missing("settings.ini.genesis-replace-backup");
    /* Interruption after committing the replacement leaves a stale backup. */
    put("settings.ini.genesis-replace-backup","older");
    assert(__wrap_rename("new.tmp","settings.ini")==0);
    expect("settings.ini","third");missing("settings.ini.genesis-replace-backup");
    /* Failed source lookup must leave an existing save untouched. */
    assert(__wrap_rename("missing.tmp","settings.ini")<0);
    expect("settings.ini","third");
    assert(__wrap_rename("settings.ini","settings.ini")==0);
    assert(__wrap_rename("missing.tmp","missing.tmp")<0);
    assert(remove("settings.ini")==0);
    puts("PASS: first save, overwrite, rollback, recovery, stale backup and missing source");
    return 0;
}

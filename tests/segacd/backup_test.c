#include "backup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
int main(int argc,char**argv)
{
    CHECK(argc==2);uint8_t live[SEGACD_BACKUP_BYTES],expected[SEGACD_BACKUP_BYTES];
    (void)remove(argv[1]);memset(live,0x5a,sizeof live);
    CHECK(segacd_backup_load(argv[1],live)==SEGACD_BACKUP_NEW&&live[0]==0x5a);
    for(size_t i=0;i<sizeof expected;++i)expected[i]=(uint8_t)(i^(i>>8));
    CHECK(segacd_backup_store(argv[1],expected)==SEGACD_BACKUP_OK);
    CHECK(segacd_backup_load(argv[1],live)==SEGACD_BACKUP_OK&&!memcmp(live,expected,sizeof live));
#ifdef _WIN32
    /* Hold the actual existing save against replacement. The completed temp
     * write must not remove/truncate it when the final replace is denied. */
    HANDLE locked=CreateFileA(argv[1],GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(locked!=INVALID_HANDLE_VALUE);
    uint8_t changed[SEGACD_BACKUP_BYTES];memset(changed,0x93,sizeof changed);
    CHECK(segacd_backup_store(argv[1],changed)==SEGACD_BACKUP_ERROR);
    CHECK(CloseHandle(locked));
    CHECK(segacd_backup_load(argv[1],live)==SEGACD_BACKUP_OK&&!memcmp(live,expected,sizeof live));
#endif
    expected[7001]^=0x55;
    CHECK(segacd_backup_store(argv[1],expected)==SEGACD_BACKUP_OK);
    CHECK(segacd_backup_load(argv[1],live)==SEGACD_BACKUP_OK&&!memcmp(live,expected,sizeof live));
    FILE*f=fopen(argv[1],"wb");CHECK(f);CHECK(fwrite(expected,1,777,f)==777&&!fclose(f));
    memset(live,0xa5,sizeof live);CHECK(segacd_backup_load(argv[1],live)==SEGACD_BACKUP_ERROR);
    for(size_t i=0;i<sizeof live;++i)CHECK(live[i]==0xa5);
    f=fopen(argv[1],"wb");CHECK(f);CHECK(fwrite(expected,1,sizeof expected,f)==sizeof expected);
    CHECK(fputc(0,f)!=EOF&&!fclose(f));
    CHECK(segacd_backup_load(argv[1],live)==SEGACD_BACKUP_ERROR&&live[0]==0xa5);
    CHECK(!remove(argv[1]));puts("backup persistence: missing, exact round trip, atomic replacement and corrupt-file preservation pass");return 0;
}

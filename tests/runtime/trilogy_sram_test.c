#include "../../sonic3k/trilogy_sram.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif

static void write_bytes(const char *p,const void *data,size_t n)
{FILE *f=fopen(p,"wb");assert(f);assert(fwrite(data,1,n,f)==n);assert(!fclose(f));}
static size_t file_size(const char *p)
{FILE *f=fopen(p,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);fclose(f);assert(n>=0);return (size_t)n;}
int main(void)
{
    uint8_t base[TR_SRAM_BASE],loaded[TR_SRAM_BASE];
    for(unsigned i=0;i<sizeof base;++i)base[i]=(uint8_t)(i*37);
    const char *path="trilogy-container.srm";
    for(unsigned packs=0;packs<4;++packs){
        remove(path);remove("trilogy-container.srm.bak");
        TrSram a={0},b={0};unsigned version=0;size_t n=0;
        memset(loaded,0xCD,sizeof loaded);assert(tr_sram_open(&a,path,loaded));
        assert(!a.exists && !a.tail_size && a.extensions_valid);assert(loaded[0]==0xCD);
        assert(tr_sram_commit(&a,base));assert(file_size(path)==TR_SRAM_BASE);
#ifdef _WIN32
        int held=-1;
        assert(!_sopen_s(&held,"trilogy-container.srm.lock",_O_WRONLY|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE));
        assert(!tr_sram_commit(&a,base));assert(!a.read_only); /* A busy lock is retryable. */
        _close(held);assert(tr_sram_commit(&a,base));
#endif
        assert(tr_sram_open(&b,path,loaded));assert(!memcmp(base,loaded,sizeof base));
        if(packs&1)assert(tr_sram_set(&a,"S1EX",1,"one",3));
        if(packs&2)assert(tr_sram_set(&a,"S2EX",1,"two",3));
        assert(tr_sram_commit(&a,base));
        /* A normal session with no donor parsing preserves the entire tail. */
        assert(tr_sram_open(&b,path,loaded));loaded[100]^=1;assert(tr_sram_commit(&b,loaded));
        assert(tr_sram_open(&a,path,loaded));
        const uint8_t *p=tr_sram_get(&a,"S1EX",&version,&n);
        assert((p!=NULL)==((packs&1)!=0));if(p)assert(n==3&&version==1&&!memcmp(p,"one",3));
        p=tr_sram_get(&a,"S2EX",&version,&n);assert((p!=NULL)==((packs&2)!=0));
        if(p)assert(n==3&&version==1&&!memcmp(p,"two",3));
        /* Unknown future content must survive known-record replacement. */
        assert(tr_sram_set(&a,"FUTR",500,"opaque",6));
        assert(tr_sram_set(&a,"S1EX",2,"longer record",13));
        assert(tr_sram_set(&a,"S1EX",1,"x",1));
        p=tr_sram_get(&a,"FUTR",&version,&n);assert(p&&version==500&&n==6&&!memcmp(p,"opaque",6));
        uint64_t generation=a.generation;assert(tr_sram_set(&a,"S1EX",1,"x",1));assert(a.generation==generation);
        TrSramRecord transaction[]={{{'S','1','E','X'},1,"changed",7},{{'S','2','E','X'},0,"invalid",7}};
        assert(!tr_sram_set_many(&a,transaction,2));assert(a.generation==generation);
        p=tr_sram_get(&a,"S1EX",&version,&n);assert(p&&n==1&&*p=='x');
        assert(tr_sram_commit(&a,base));
        assert(!tr_sram_commit(&b,base));assert(b.read_only); /* stale writer */
        assert(tr_sram_open(&b,path,loaded));assert(!memcmp(base,loaded,sizeof base));
        p=tr_sram_get(&b,"S1EX",&version,&n);assert(p&&n==1&&*p=='x');
        /* Every damaged tail byte disables extension interpretation, but a
         * base-only write must preserve the damaged tail exactly. */
        size_t total=b.original_size;uint8_t *damaged=malloc(total);assert(damaged);
        size_t backup_size=file_size("trilogy-container.srm.bak");
        uint8_t *backup=malloc(backup_size),*after=malloc(backup_size);assert(backup&&after);
        FILE *bf=fopen("trilogy-container.srm.bak","rb");assert(bf);
        assert(fread(backup,1,backup_size,bf)==backup_size);fclose(bf);
        for(size_t at=TR_SRAM_BASE;at<total;++at){
            memcpy(damaged,b.original,total);damaged[at]^=1;write_bytes(path,damaged,total);
            assert(tr_sram_open(&a,path,loaded));assert(!a.extensions_valid);
            assert(!tr_sram_set(&a,"S2EX",1,"no",2));assert(!tr_sram_get(&a,"S1EX",NULL,NULL));
            loaded[20]^=1;assert(tr_sram_commit(&a,loaded));
            assert(a.original_size==total&&!memcmp(a.original+TR_SRAM_BASE,damaged+TR_SRAM_BASE,total-TR_SRAM_BASE));
        }
        assert(file_size("trilogy-container.srm.bak")==backup_size);
        bf=fopen("trilogy-container.srm.bak","rb");assert(bf);
        assert(fread(after,1,backup_size,bf)==backup_size);fclose(bf);assert(!memcmp(backup,after,backup_size));
        free(backup);free(after);
        free(damaged);tr_sram_dispose(&a);tr_sram_dispose(&b);
    }
    TrSram bad={0};memset(loaded,0xAB,sizeof loaded);write_bytes(path,"short",5);
    assert(!tr_sram_open(&bad,path,loaded));assert(bad.read_only&&loaded[0]==0xAB);
    assert(!tr_sram_commit(&bad,base));assert(file_size(path)==5);tr_sram_dispose(&bad);
    remove(path);remove("trilogy-container.srm.bak");remove("trilogy-container.srm.lock");
    puts("trilogy SRAM: all pack permutations, native prefix, future records, corruption preservation and stale writer checks passed");
    return 0;
}

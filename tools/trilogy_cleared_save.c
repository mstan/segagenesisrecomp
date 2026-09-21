/* Create one explicitly requested cleared slot in an offline save copy.
 * Native S3K primary/backup checksums follow Create_SRAMChecksum. */
#include "../sonic3k/trilogy_progress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned word(const uint8_t *p){return p[0]*256u+p[1];}
static unsigned checksum(const uint8_t *p)
{unsigned crc=0;for(unsigned i=0;i<82;i+=2){crc^=word(p+i);unsigned bit=crc&1;crc>>=1;if(bit)crc^=0x8810;}return crc;}
int main(int argc,char **argv)
{
    if(argc!=3){fprintf(stderr,"usage: trilogy_cleared_save offline-copy.srm slot-1-through-8\n");return 2;}
    char *end;unsigned slot=(unsigned)strtoul(argv[2],&end,10);if(*end||slot<1||slot>8)return 2;--slot;
    uint8_t base[TR_SRAM_BASE],native[84];TrSram s={0};TrProgress p={0};TrResume next;int ok=0;
    if(!tr_sram_open(&s,argv[1],base)||!tr_progress_load(&p,&s))goto done;
    for(unsigned i=0;i<84;++i)native[i]=base[0x281+i*2];
    if(word(native+82)!=checksum(native)){fprintf(stderr,"Native primary checksum invalid; leave save unchanged\n");goto done;}
    uint8_t *n=native+slot*10;memset(n,0,10);n[0]=2;n[3]=14;n[6]=0x55;n[7]=0x54;n[8]=3;
    if(!tr_progress_new(&p,slot,3,TR_SONIC_TAILS))goto done;
    if(!tr_progress_collect(&p,slot,127,0))goto done;
    for(unsigned i=0;i<4;++i)if(!tr_progress_finish_import(&p,slot,3,&next))goto done;
    if(!tr_progress_sync_native(&p,slot,n)||p.campaign.slots[slot].state!=TR_COMPLETE)goto done;
    if(!tr_progress_select_zone(&p,slot,0x1000,3)||!tr_progress_store(&p,&s))goto done;
    unsigned crc=checksum(native);native[82]=(uint8_t)(crc>>8);native[83]=(uint8_t)crc;
    for(unsigned i=0;i<84;++i){base[0x281+i*2]=native[i];base[0x32D+i*2]=native[i];}
    ok=tr_sram_commit(&s,base);
    if(ok)printf("Slot %u: cleared Sonic & Tails, both donors, seven Chaos Emeralds; stage browser starts at GHZ1\n",slot+1);
done:if(!ok)fprintf(stderr,"Cleared save failed: %s %s\n",p.error,s.error);tr_sram_dispose(&s);return ok?0:1;
}

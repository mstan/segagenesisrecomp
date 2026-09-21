/* Advance two slots in an isolated, freshly generated playtest fixture.
 * The native prefix/checksums come from the actual Data Select menu. */
#include "../sonic3k/trilogy_progress.h"
#include <stdio.h>
int main(int argc,char **argv)
{
    if(argc!=2){fprintf(stderr,"usage: trilogy_playtest_seed fresh-fixture.srm\n");return 2;}
    uint8_t base[TR_SRAM_BASE];TrSram s={0};TrProgress p;TrResume resume;
    if(!tr_sram_open(&s,argv[1],base)||!tr_progress_load(&p,&s))return 1;
    for(unsigned i=0;i<8;++i)if(!p.tokens[i]||p.campaign.slots[i].state!=TR_ACTIVE||
        p.campaign.slots[i].stage!=((i==3||i==7)?0x2000:0x1000)){
        fprintf(stderr,"not the expected fresh eight-slot fixture\n");tr_sram_dispose(&s);return 1;}
    int ok=tr_progress_finish_import(&p,1,3,&resume)&&resume.stage==0x1001&&
        tr_progress_finish_import(&p,2,3,&resume)&&resume.stage==0x1001&&
        tr_progress_finish_import(&p,2,3,&resume)&&resume.stage==0x1002&&
        tr_progress_store(&p,&s)&&tr_sram_commit(&s,base);
    tr_sram_dispose(&s);return ok?0:1;
}

#define main vdp_existing_differential_main
#include "vdp_render_diff_test.c"
#undef main
void cand_gvdp_render_reuse_clear(void);
void cand_gvdp_render_reuse_stats(uint64_t out[6]);
size_t cand_gvdp_render_reuse_cache_bytes(void);
unsigned gvdp_test_alloc_attempts(void);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"allocation fallback line%u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    GVDP a,b;uint8_t x[GVDP_MAX_WIDTH],y[GVDP_MAX_WIDTH];uint64_t stats[6];
    fill_random(&a,1,1,0,1,0,0,0);a.reg[5]=0x30;install_directed_sprite(&a,1,0,0);
    cand_gvdp_render_reuse_clear();
    for(unsigned n=0;n<6;++n) {
        a.sprite_collision=a.sprite_overflow=0;a.reg[7]=(uint8_t)n;b=a;GVDPRasterRow row;memset(&row,0xa6,sizeof row);
        memset(x,0xa5,sizeof x);memset(y,0xa5,sizeof y);
        CHECK(head_gvdp_render_scanline(&a,10,x)==320);CHECK(cand_gvdp_capture_scanline(&b,10,&row)==320);
        CHECK(a.sprite_collision && b.sprite_collision);CHECK(!memcmp(&a,&b,sizeof a));
        CHECK(cand_gvdp_compose_captured(&row,y)==320);CHECK(!memcmp(x,y,sizeof x));
        CHECK(cand_gvdp_render_reuse_cache_bytes()==0);
    }
    cand_gvdp_render_reuse_stats(stats);CHECK(stats[0]==6 && stats[1]==0 && stats[2]==0 && stats[3]==6 && stats[4]==0 && stats[5]==0);
    CHECK(gvdp_test_alloc_attempts()==1);
    puts("Capture allocation failure: one allocation attempt, exact original fallback and fresh status for all rows");return 0;
}

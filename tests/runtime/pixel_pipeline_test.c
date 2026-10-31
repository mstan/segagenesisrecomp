/* Frozen-original conversion/staging oracle; actual presentation has separate QA. */
#include "genesis_frame_pixels.h"
#include "color_lut.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int original_inverse(uint32_t value);
void original_color_row(const void *,const void *,void *,int);
const uint32_t *original_prepare(const void *,const uint32_t *,uint32_t *,int,int,int,int,int,int);
static void new_color_row(void *lut,const void *src,void *dst,int count)
{
    const uint32_t *s=(const uint32_t *)src;uint32_t *d=(uint32_t *)dst;
    for(int x=0;x<count;++x)d[x]=color_lut_map_argb((const ColorLut *)lut,s[x]);
}
static uint32_t random_value(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static void color_domain(void)
{
    ColorLut lut;uint32_t seed=17,src[4096],a[4096],b[4096];
    for(unsigned i=0;i<512;++i)lut.table[i]=random_value(&seed);
    for(uint32_t v=0;v<256;++v){
        CHECK(genesis_dac_nearest_normal_idx(v)==original_inverse(v));
#if GENESIS_FINAL_PIXEL_PIPELINE
        CHECK(genesis_dac_inverse8[v]==original_inverse(v));
#endif
    }
    const uint32_t broad[]={256,510,511,65535,0x7fffffffu,0xfffffffeu,0xffffffffu};
    for(unsigned i=0;i<sizeof broad/sizeof broad[0];++i)CHECK(genesis_dac_nearest_normal_idx(broad[i])==original_inverse(broad[i]));
    for(int pass=0;pass<16;++pass){
        for(unsigned i=0;i<4096;++i)src[i]=random_value(&seed);
        original_color_row(&lut,src,a,4096);new_color_row(&lut,src,b,4096);CHECK(!memcmp(a,b,sizeof a));
    }
    /* Every final512table index, plus all alpha bytes with unchanged RGB. */
    for(unsigned i=0;i<512;++i)src[i]=genesis_dac_cram_to_argb((uint16_t)(((i&7u)<<1)|((i&56u)<<2)|((i&448u)<<3)),0);
    for(unsigned i=512;i<4096;++i)src[i]=(i<<24)|0x00737782u;
    original_color_row(&lut,src,a,4096);new_color_row(&lut,src,b,4096);CHECK(!memcmp(a,b,sizeof a));
}
static void mark_ui(uint32_t *pixels,int stride,int width,int height)
{
    /* A legal active-only writable overlay; raw input must remain untouched. */
    for(int y=0;y<height;y+=17)for(int x=0;x<width;x+=13)pixels[(size_t)y*stride+x]^=0x00f0813du;
}
static void transitions(void)
{
    const int stride=1024,storage_h=480;size_t count=(size_t)stride*storage_h,bytes=count*sizeof(uint32_t);
    uint32_t *raw=(uint32_t *)malloc(bytes),*copy=(uint32_t *)malloc(bytes),*a=(uint32_t *)malloc(bytes),*b=(uint32_t *)malloc(bytes);CHECK(raw&&copy&&a&&b);
    ColorLut lut;uint32_t seed=31;for(unsigned i=0;i<512;++i)lut.table[i]=random_value(&seed);
    for(size_t i=0;i<count;++i){raw[i]=random_value(&seed);a[i]=b[i]=0xcca51e00u+(uint32_t)i;}
    const int widths[]={320,928,256,1024,400,512},heights[]={224,448,240,224,448,240};
    for(int frame=0;frame<48;++frame){
        int w=widths[frame%6],h=heights[frame%6],ui=(frame/3)&1,color=(frame/2)&1;GenesisPixelView view;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)raw[(size_t)y*stride+x]=random_value(&seed);
        memcpy(copy,raw,bytes);
        const uint32_t *expected=original_prepare(&lut,raw,a,stride,storage_h,w,h,color,ui);
        CHECK(genesis_frame_pixels(&view,raw,stride*4,b,bytes,stride*4,w,h,stride,storage_h,ui,color?new_color_row:NULL,&lut));
        CHECK(view.writable==(color||ui));CHECK(view.pitch_bytes==stride*4);
        if(ui){CHECK(view.pixels==b);mark_ui(a,stride,w,h);mark_ui(b,stride,w,h);}
        CHECK(!memcmp(expected,view.pixels,bytes)); /* Includes linear-filter storage edges/history. */
        CHECK(!memcmp(raw,copy,bytes));
    }
    free(raw);free(copy);free(a);free(b);
}
static void pitch_rejection(void)
{
    union Storage { GenesisPixelView alignment;unsigned char bytes[128]; } raw_storage,stage_storage;
    unsigned char *raw=raw_storage.bytes,*stage=stage_storage.bytes,before[128],raw_before[128];GenesisPixelView view;
    ColorLut lut={{0},false};
    for(unsigned i=0;i<128;++i)raw[i]=(unsigned char)i;
    memcpy(raw_before,raw,sizeof raw_before);
    memset(stage,0xc9,sizeof before);memcpy(before,stage,sizeof before);
    CHECK(genesis_frame_pixels(&view,raw,19,stage,sizeof before,16,4,3,4,3,0,NULL,NULL));
    CHECK(view.writable && view.pitch_bytes==16);
    for(int y=0;y<3;++y)CHECK(!memcmp(stage+y*16,raw+y*19,16));
    CHECK(!memcmp(stage+48,before+48,sizeof before-48));
    memcpy(before,stage,sizeof before);
    CHECK(!genesis_frame_pixels(&view,raw,19,stage,47,16,4,3,4,3,1,NULL,NULL));
    CHECK(!genesis_frame_pixels(&view,raw,15,stage,sizeof before,16,4,3,4,3,1,NULL,NULL));
    CHECK(!genesis_frame_pixels(&view,raw,16,raw+4,124,16,4,3,4,3,1,NULL,NULL));
    CHECK(!genesis_frame_pixels((GenesisPixelView *)raw,raw,16,stage,sizeof before,16,4,3,4,3,0,NULL,NULL));
    CHECK(!genesis_frame_pixels((GenesisPixelView *)stage,raw,16,stage,sizeof before,16,4,3,4,3,1,NULL,NULL));
    /* Inactive raw storage remains immutable, even on a borrowed active row. */
    CHECK(!genesis_frame_pixels((GenesisPixelView *)(raw+64),raw,16,stage,sizeof before,16,4,1,4,8,0,NULL,NULL));
    CHECK(!genesis_frame_pixels(&view,raw,16,raw+64,64,16,4,1,4,8,1,new_color_row,&lut));
    CHECK(!genesis_frame_pixels(&view,raw,16,stage,sizeof before,16,INT_MAX,3,4,3,1,NULL,NULL));
    CHECK(!memcmp(stage,before,sizeof before));
    CHECK(!memcmp(raw,raw_before,sizeof raw_before));
}
int main(void){color_domain();transitions();pitch_rejection();puts("Pixel conversion, ownership, full-storage history, oddpitch and rejection checks pass");return 0;}

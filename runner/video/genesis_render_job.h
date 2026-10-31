/* Host-only complete indexed raster jobs. Included after the existing sprite
 * evaluator/plane service: no guest state, writer epochs or caller pointers.
 * Current commands plus fresh sprite lanes are an exact value identity.
 * CRAM/host colors stay at the original live/per-capture ARGB conversion. */
#ifndef GENESIS_RENDER_REUSE
#define GENESIS_RENDER_REUSE 1
#endif
#ifndef GENESIS_RENDER_REUSE_STATS
#define GENESIS_RENDER_REUSE_STATS 0
#endif
#if GENESIS_RENDER_REUSE
#define GVDP_JOB_COMMANDS (GVDP_MAX_WIDTH / 2)
typedef struct {
    uint32_t bits;
    uint16_t attr,x;
    uint8_t count,fx,plane,ext;
} GVDPPlaneCommand;
_Static_assert(sizeof(GVDPPlaneCommand)==12,"Canonical plane command has no padding");
typedef struct {
    uint32_t width,start,count,diag_offset,diag_mask;
    uint8_t backdrop,bar,shadow,diagnostic,extended,reserved[3];
} GVDPJobKey;
typedef struct {
    int line,w,extra,start,end,im2,wt,ht,win_wt,hs_a,hs_b;
    int two_cell,vs_mask,full_a,full_b,window_x,right,vertical;
    uint16_t base_a,base_b,base_w;
} GVDPJobGeometry;
typedef struct {
    GVDPJobKey key;
    GVDPPlaneCommand commands[GVDP_JOB_COMMANDS];
    uint8_t sprites[5][GVDP_MAX_WIDTH];
    uint8_t planes[2][GVDP_MAX_WIDTH],indices[GVDP_MAX_WIDTH];
    unsigned command_count,valid,indices_ready,cooldown;
} GVDPJobResult;
static GVDPJobResult s_job_results[GVDP_MAX_HEIGHT*2];
#if GENESIS_RENDER_REUSE_STATS
static uint64_t s_job_stats[6];
#define JOB_STAT(n) (++s_job_stats[n])
void gvdp_render_reuse_stats(uint64_t out[6]){memcpy(out,s_job_stats,sizeof s_job_stats);}
void gvdp_render_reuse_clear(void){memset(s_job_results,0,sizeof s_job_results);memset(s_job_stats,0,sizeof s_job_stats);}
#else
#define JOB_STAT(n) ((void)0)
#endif
static int job_overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x<an:x-y<bn;
}
static int job_output_safe(const GVDP *v,const void *out,size_t bytes)
{
    if(!out || job_overlap(out,bytes,v,sizeof *v) ||
       (s_ext_tiles && job_overlap(out,bytes,s_ext_tiles,2048)))return 0;
    const uint8_t *sprites[5]={s_spr_idx,s_spr_op,s_spr_hi,s_spr_shadow_op,s_spr_hilite_op};
    for(unsigned i=0;i<5;++i)if(job_overlap(out,bytes,sprites[i],GVDP_MAX_WIDTH))return 0;
    return 1;
}
static int job_plane_commands(const GVDP *v,GVDPPlaneCommand *commands,unsigned *used,
    unsigned plane,int xo,int count,uint16_t base,int wt,int ht,int px,int py,int im2)
{
    int shift=im2?4:3,fy_mask=(1<<shift)-1,tile_bytes=im2?64:32,mask=wt*8-1;
    py&=(ht<<shift)-1;
    uint16_t row_base=(uint16_t)(base+(py>>shift)*wt*2);
    while(count) {
        px&=mask;int fx=px&7,n=8-fx;if(n>count)n=count;
        if(*used>=GVDP_JOB_COMMANDS)return 0;
        uint16_t attr=vram_read_word(v,(uint16_t)(row_base+(px>>3)*2));
        int fy=py&fy_mask;if(attr&0x1000)fy^=fy_mask;
        uint16_t pa=(uint16_t)((attr&0x7ff)*tile_bytes+fy*4);
        GVDPPlaneCommand *c=&commands[(*used)++];
        c->bits=((uint32_t)v->vram[pa]<<24)|((uint32_t)v->vram[pa+1]<<16)|
                ((uint32_t)v->vram[pa+2]<<8)|v->vram[pa+3];
        c->attr=attr;c->x=(uint16_t)xo;c->count=(uint8_t)n;c->fx=(uint8_t)fx;
        c->plane=(uint8_t)plane;c->ext=(uint8_t)(!(attr&0x6000)?gvdp_ext_line(attr&0x7ff):0);
        xo+=n;px+=n;count-=n;
    }
    return 1;
}
static int job_prepare(const GVDP *v,const GVDPJobGeometry *g,GVDPPlaneCommand *commands,unsigned *used)
{
    *used=0;
    for(int x=-g->extra,xo=g->start;xo<g->end;) {
        int boundary=x<0?0:x>=g->w?g->w+g->extra:((x>>4)+1)*16;
        if(boundary>g->w+g->extra)boundary=g->w+g->extra;
        if(g->window_x>x && g->window_x<boundary)boundary=g->window_x;
        int n=boundary-x,va=g->full_a,vb=g->full_b;
        if(g->two_cell) {
            int xc=x<0?0:x>=g->w?g->w-1:x,col=(xc>>4)*2;
            va=v->vsram[col%GVDP_VSRAM_ENTRIES]&g->vs_mask;
            vb=v->vsram[(col+1)%GVDP_VSRAM_ENTRIES]&g->vs_mask;
        }
        int horizontal=g->right?x>=g->window_x:x<g->window_x;
        int ok;
        if(x>=0 && x<g->w && (horizontal||g->vertical))
            ok=job_plane_commands(v,commands,used,0,xo,n,g->base_w,g->win_wt,g->ht,x,g->line,g->im2);
        else ok=job_plane_commands(v,commands,used,0,xo,n,g->base_a,g->wt,g->ht,x-g->hs_a,g->line+va,g->im2);
        if(!ok || !job_plane_commands(v,commands,used,1,xo,n,g->base_b,g->wt,g->ht,x-g->hs_b,g->line+vb,g->im2))return 0;
        x+=n;xo+=n;
    }
    return 1;
}
static void job_decode(const GVDPPlaneCommand *commands,unsigned count,
    uint8_t planes[2][GVDP_MAX_WIDTH],uint8_t (*full)[GVDP_MAX_WIDTH])
{
    for(unsigned i=0;i<count;++i) {
        const GVDPPlaneCommand *c=&commands[i];
        uint8_t attr=(uint8_t)(((c->attr>>9)&0x30)|((c->attr>>8)&0x80));
#if GENESIS_NATIVE_TILE_ROWS
        uint64_t pixels=gtile_packed_row(c->bits,attr,c->attr&0x800);
        memcpy(planes[c->plane]+c->x,(const uint8_t *)&pixels+c->fx,c->count);
#else
        unsigned flip=(c->attr&0x800)?7u:0u;
        for(unsigned n=0;n<c->count;++n) {
            unsigned nib=(c->bits>>(28-((c->fx+n)^flip)*4))&15u;
            planes[c->plane][c->x+n]=(uint8_t)(attr|nib|(nib?0x40:0));
        }
#endif
        if(full)for(unsigned n=0;n<c->count;++n) {
            unsigned pixel=planes[c->plane][c->x+n]&63u;
            if(c->ext && (pixel&15u))pixel=GVDP_HOST_PALETTE_BASE+(c->ext-1u)*16u+(pixel&15u);
            full[c->plane][c->x+n]=(uint8_t)pixel;
        }
    }
}
static void job_compose(const GVDPJobKey *key,const uint8_t *a,const uint8_t *b,
    const uint8_t *full_a,const uint8_t *full_b,uint8_t *out)
{
    memset(out,key->bar,key->width);
    unsigned begin=key->start,end=begin+key->count;
#if GENESIS_BATCHED_PLANES && GENESIS_NATIVE_COMPOSE
    if(!key->extended && !key->diagnostic) {
        gvdp_compose_span(out+begin,a+begin,b+begin,s_spr_idx+begin,s_spr_op+begin,
            s_spr_hi+begin,s_spr_shadow_op+begin,s_spr_hilite_op+begin,
            (int)key->count,key->backdrop,key->shadow);
        return;
    }
#endif
    for(unsigned x=begin;x<end;++x) {
        unsigned ai=key->extended?full_a[x]:a[x]&63u,bi=key->extended?full_b[x]:b[x]&63u;
        int ao=(a[x]&64)!=0,ah=(a[x]&128)!=0,bo=(b[x]&64)!=0,bh=(b[x]&128)!=0;
        int so=s_spr_op[x],sh=s_spr_hi[x];unsigned pixel;
        if(so&&sh)pixel=s_spr_idx[x];else if(ao&&ah)pixel=ai;else if(bo&&bh)pixel=bi;
        else if(so)pixel=s_spr_idx[x];else if(ao)pixel=ai;else if(bo)pixel=bi;else pixel=key->backdrop;
        if(key->shadow && pixel<64) {
            int shadow=!((so&&sh)||(ao&&ah)||(bo&&bh)),hilite=0;
            if(s_spr_shadow_op[x])shadow=1;
            if(s_spr_hilite_op[x]){if(shadow)shadow=0;else hilite=1;}
            if(hilite)pixel+=128;else if(shadow)pixel+=64;
        }
        if(key->diagnostic && pixel<GVDP_HOST_PALETTE_BASE && !((x-key->diag_offset)&key->diag_mask))
            pixel=(pixel&63u)+64u;
        out[x]=(uint8_t)pixel;
    }
}
/* Called only after the original sprite/status/callback work. A zero return
 * retains the original plane/composition path WITHOUT repeating that work. */
static int job_result(const GVDP *v,const GVDPJobGeometry *g,const GVDPJobKey *key,
    uint8_t *out,uint8_t *capture_a,uint8_t *capture_b)
{
    if(g->line<0 || g->line>=GVDP_MAX_HEIGHT*2 || !key->width || key->width>GVDP_MAX_WIDTH ||
       key->start>key->width || key->count>key->width-key->start)return 0;
    GVDPJobResult *cached=&s_job_results[g->line];JOB_STAT(0);
    if(cached->cooldown){--cached->cooldown;JOB_STAT(3);return 0;}
    GVDPPlaneCommand commands[GVDP_JOB_COMMANDS];unsigned used;
    if(!job_prepare(v,g,commands,&used))return 0;
    const uint8_t *sprites[5]={s_spr_idx,s_spr_op,s_spr_hi,s_spr_shadow_op,s_spr_hilite_op};
    int hit=cached->valid && !memcmp(&cached->key,key,sizeof *key) && cached->command_count==used &&
        !memcmp(cached->commands,commands,used*sizeof *commands);
    if(hit)for(unsigned i=0;i<5;++i)if(memcmp(cached->sprites[i],sprites[i],key->width)){hit=0;break;}
    if(hit) {
        JOB_STAT(1);
        if(out) {
            if(!cached->indices_ready) {
                /* Capture rejects extended providers, so a cached capture can
                 * never require unavailable full-index extension planes. */
                if(key->extended)return 0;
                job_compose(key,cached->planes[0],cached->planes[1],NULL,NULL,cached->indices);
                cached->indices_ready=1;
            }
            memcpy(out,cached->indices,key->width);JOB_STAT(4);
        } else {
            memcpy(capture_a+key->start,cached->planes[0]+key->start,key->count);
            memcpy(capture_b+key->start,cached->planes[1]+key->start,key->count);JOB_STAT(5);
        }
        return 1;
    }
    JOB_STAT(2);
    unsigned cooldown=cached->valid?31u:0u;
    uint8_t planes[2][GVDP_MAX_WIDTH],full[2][GVDP_MAX_WIDTH];
    job_decode(commands,used,planes,out&&key->extended?full:NULL);
    if(out)job_compose(key,planes[0],planes[1],full[0],full[1],out);
    else {
        memcpy(capture_a+key->start,planes[0]+key->start,key->count);
        memcpy(capture_b+key->start,planes[1]+key->start,key->count);
    }
    cached->key=*key;cached->command_count=used;
    memcpy(cached->commands,commands,used*sizeof *commands);
    for(unsigned i=0;i<5;++i)memcpy(cached->sprites[i],sprites[i],key->width);
    for(unsigned i=0;i<2;++i)memcpy(cached->planes[i]+key->start,planes[i]+key->start,key->count);
    if(out)memcpy(cached->indices,out,key->width);
    cached->indices_ready=out!=NULL;cached->cooldown=cooldown;cached->valid=1;
    return 1;
}
#endif /* GENESIS_RENDER_REUSE */

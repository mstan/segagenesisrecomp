#include "genesis_raster_jobs.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
typedef volatile long HdAtomic;
static int atom_get(HdAtomic *p) { return (int)_InterlockedCompareExchange(p,0,0); }
static int atom_cas(HdAtomic *p,int old,int value) { return _InterlockedCompareExchange(p,value,old)==old; }
static void atom_set(HdAtomic *p,int value) { (void)_InterlockedExchange(p,value); }
#elif defined(__GNUC__) || defined(__clang__)
typedef int HdAtomic;
static int atom_get(HdAtomic *p) { return __atomic_load_n(p,__ATOMIC_ACQUIRE); }
static int atom_cas(HdAtomic *p,int old,int value) { return __atomic_compare_exchange_n(p,&old,value,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE); }
static void atom_set(HdAtomic *p,int value) { __atomic_store_n(p,value,__ATOMIC_RELEASE); }
#else
#include <stdatomic.h>
typedef atomic_int HdAtomic;
static int atom_get(HdAtomic *p) { return atomic_load(p); }
static int atom_cas(HdAtomic *p,int old,int value) { return atomic_compare_exchange_strong(p,&old,value); }
static void atom_set(HdAtomic *p,int value) { atomic_store(p,value); }
#endif

/* Exactly three bounded native-density frames. A writer reserves a slot before
 * mutation; acquiring a published slot and writer reservation use the same CAS,
 * so a delayed reader cannot race reuse of an old published index. */
#define HD_FRAME_SLOTS 3
#define HD_WRITING (-1073741824)
#define HD_TOMBSTONE UINT32_MAX
typedef struct HdFrameSlot {
    HdAtomic state;
    unsigned captured[GVDP_HD_ROWS], bound[GVDP_HD_ROWS], bound_count;
    GVDPRasterFrame frame;
} HdFrameSlot;
static HdFrameSlot *s_frames;
static HdAtomic s_published=-1;
static HdFrameSlot *s_build;
static const GVDP *s_owner;
static unsigned s_line, s_enabled;
unsigned g_gvdp_hd_capture_active;
#define s_active g_gvdp_hd_capture_active
static uint64_t s_serial;
static GVDPHdSpan s_a[GVDP_HD_ROW_SPANS],s_b[GVDP_HD_ROW_SPANS],s_s[GVDP_HD_ROW_SPANS];
static unsigned s_counts[3];

typedef struct HdCacheMeta { uint64_t key,last; unsigned occupied; } HdCacheMeta;
typedef struct HdCache {
    HdCacheMeta *meta;
    uint32_t *table,*values;
    unsigned capacity,table_size,words,next,tombstones;
} HdCache;
static HdCacheMeta s_pattern_meta[GVDP_HD_PATTERNS],s_palette_meta[GVDP_HD_PALETTES];
static uint32_t s_pattern_table[GVDP_HD_PATTERNS*2],s_palette_table[GVDP_HD_PALETTES*2];
static uint32_t s_patterns[GVDP_HD_PATTERNS];
static uint32_t s_palettes[GVDP_HD_PALETTES][GVDP_OUTPUT_PALETTE_SIZE];
static HdCache s_pattern_cache={s_pattern_meta,s_pattern_table,s_patterns,GVDP_HD_PATTERNS,GVDP_HD_PATTERNS*2,1,0,0};
static HdCache s_palette_cache={s_palette_meta,s_palette_table,&s_palettes[0][0],GVDP_HD_PALETTES,GVDP_HD_PALETTES*2,GVDP_OUTPUT_PALETTE_SIZE,0,0};

static unsigned cache_hash(uint64_t x,unsigned mask)
{
    x^=x>>33;x*=UINT64_C(0xff51afd7ed558ccd);x^=x>>33;
    return (unsigned)x&mask;
}
static void cache_rehash(HdCache *c)
{
    if(c->tombstones<c->table_size/4)return;
    memset(c->table,0,(size_t)c->table_size*sizeof *c->table);
    for(unsigned i=0;i<c->capacity;++i)if(c->meta[i].occupied) {
        unsigned p=cache_hash(c->meta[i].key,c->table_size-1);
        while(c->table[p])p=(p+1)&(c->table_size-1);
        c->table[p]=i+1;
    }
    c->tombstones=0;
}
static unsigned cache_intern(HdCache *c,uint64_t key,const uint32_t *value,int *first)
{
    uint64_t epoch=s_build->frame.serial;
    unsigned mask=c->table_size-1,p=cache_hash(key,mask),insert=UINT_MAX;
    for(unsigned n=0;n<c->table_size;++n,p=(p+1)&mask) {
        unsigned entry=c->table[p];
        if(!entry){if(insert==UINT_MAX)insert=p;break;}
        if(entry==HD_TOMBSTONE){if(insert==UINT_MAX)insert=p;continue;}
        unsigned i=entry-1;
        if(c->meta[i].key==key&&!memcmp(c->values+(size_t)i*c->words,value,(size_t)c->words*4)) {
            *first=c->meta[i].last!=epoch;c->meta[i].last=epoch;return i;
        }
    }
    if(insert==UINT_MAX)return UINT_MAX;
    unsigned chosen=UINT_MAX;
    for(unsigned n=0;n<c->capacity;++n) {
        unsigned i=c->next++&(c->capacity-1);
        if(!c->meta[i].occupied||c->meta[i].last!=epoch){chosen=i;break;}
    }
    if(chosen==UINT_MAX)return UINT_MAX;
    if(c->meta[chosen].occupied) {
        p=cache_hash(c->meta[chosen].key,mask);
        for(unsigned n=0;n<c->table_size;++n,p=(p+1)&mask)
            if(c->table[p]==chosen+1){c->table[p]=HD_TOMBSTONE;++c->tombstones;break;}
    }
    if(c->table[insert]==HD_TOMBSTONE)--c->tombstones;
    c->table[insert]=chosen+1;c->meta[chosen].key=key;
    c->meta[chosen].last=epoch;c->meta[chosen].occupied=1;
    memcpy(c->values+(size_t)chosen*c->words,value,(size_t)c->words*4);
    *first=1;return chosen;
}
static void cancel_build(void)
{
    s_active=0;
    if(s_build){s_build->frame.valid=0;atom_set(&s_build->state,0);s_build=NULL;}
}
void gvdp_hd_reset(void)
{
    atom_set(&s_published,-1);cancel_build();s_owner=NULL;
}
void gvdp_hd_enable(int enabled)
{
    unsigned value=enabled!=0;
    /* Native/default presentation pays no frame-pool allocation. The bounded
     * pool, once enabled, remains alive until process shutdown so concurrent
     * leases/acquirers cannot race deallocation during a live scale toggle. */
    if(value&&!s_frames) {
        s_frames=(HdFrameSlot *)calloc(HD_FRAME_SLOTS,sizeof *s_frames);
        if(!s_frames)value=0;
    }
    if(s_enabled!=value){gvdp_hd_reset();s_enabled=value;}
}
void gvdp_hd_ineligible(void)
{
    if(s_build)s_build->frame.valid=0;
    s_active=0;
}
void gvdp_hd_begin_row(const GVDP *v,unsigned line,unsigned width,
                      unsigned start,unsigned count,unsigned backdrop,
                      unsigned bar,unsigned display,unsigned shadow)
{
    s_active=0;if(!s_enabled)return;
    if(!line) {
        cancel_build();s_owner=v;cache_rehash(&s_pattern_cache);cache_rehash(&s_palette_cache);
        int published=atom_get(&s_published);
        for(int i=0;i<HD_FRAME_SLOTS;++i)
            if(i!=published&&atom_cas(&s_frames[i].state,0,HD_WRITING)){s_build=&s_frames[i];break;}
        if(!s_build){atom_set(&s_published,-1);return;}
        GVDPRasterFrame *f=&s_build->frame;
        f->serial=++s_serial;f->width=width;f->height=(unsigned)gvdp_output_height(v);
        f->span_count=f->pattern_count=f->palette_count=f->row_count=0;f->valid=1;
        memset(s_build->captured,0,sizeof s_build->captured);
        memset(s_build->bound,0,sizeof s_build->bound);s_build->bound_count=0;
    }
    if(!s_build||s_owner!=v)return;
    GVDPRasterFrame *f=&s_build->frame;
    if(!f->valid||!width||width>GVDP_MAX_WIDTH||f->height>GVDP_HD_ROWS||
       line>=f->height||width!=f->width||start>width||count>width-start) {
        gvdp_hd_ineligible();return;
    }
    s_line=line;s_counts[0]=s_counts[1]=s_counts[2]=0;
    GVDPHdRow *r=&f->rows[line];memset(r,0,sizeof *r);
    r->width=width;r->start=start;r->count=count;r->backdrop=backdrop;
    r->bar=bar;r->display=display;r->shadow_mode=shadow;
    r->ops_first=line*(GVDP_MAX_WIDTH/8);
    memset(f->operator_words+r->ops_first,0,((width+7)/8)*sizeof(uint32_t));
    s_active=1;
}
void gvdp_hd_set_diagnostic(unsigned enabled,unsigned offset,unsigned mask)
{
    if(!s_active)return;
    GVDPHdRow *r=&s_build->frame.rows[s_line];r->diagnostic=enabled;r->diag_offset=offset;r->diag_mask=mask;
}
void gvdp_hd_emit(unsigned layer,unsigned x,unsigned count,uint32_t bits,
                  unsigned first,unsigned flip,unsigned palette,unsigned high,unsigned visible)
{
    if(!s_active||layer>2||!count||!visible)return;
    if(count>8||first+count>8||x>=s_build->frame.width||count>s_build->frame.width-x) {
        gvdp_hd_ineligible();return;
    }
    GVDPHdSpan *list=layer==0?s_a:layer==1?s_b:s_s;
    unsigned n=s_counts[layer];
    /* The scalar fallback observes one native pixel at a time. Adjacent pixels
     * from the same prepared source are coalesced into the same compact job. */
    if(layer<2&&n) {
        GVDPHdSpan *last=&list[n-1];unsigned old_count=last->run&255u;
        if(last->x+old_count==x&&old_count+count<=8&&
           ((last->run>>8)&255u)+old_count==first&&((last->run>>16)&1u)==!!flip&&
           (last->color&511u)==((palette&255u)|((!!high)<<8))&&
           s_build->frame.pattern_values[last->source]==bits) {
            last->run=(last->run&~255u)|(old_count+count);return;
        }
    }
    if(n>=GVDP_HD_ROW_SPANS){gvdp_hd_ineligible();return;}
    int used;unsigned source=cache_intern(&s_pattern_cache,bits,&bits,&used);
    if(source==UINT_MAX){gvdp_hd_ineligible();return;}
    GVDPRasterFrame *f=&s_build->frame;
    if(used){f->pattern_values[source]=bits;f->used_patterns[f->pattern_count++]=source;}
    list[n].x=x;list[n].source=source;
    list[n].run=count|(first<<8)|((!!flip)<<16);
    list[n].color=(palette&255u)|((!!high)<<8)|((visible&255u)<<16);
    s_counts[layer]=n+1;
}
void gvdp_hd_operator(unsigned x,unsigned highlight)
{
    if(!s_active||x>=s_build->frame.width)return;
    GVDPHdRow *r=&s_build->frame.rows[s_line];
    s_build->frame.operator_words[r->ops_first+x/8]|=1u<<((x&7)+(highlight?8:0));
}
static int compare_span(const void *a,const void *b)
{
    const GVDPHdSpan *x=a,*y=b;return (x->x>y->x)-(x->x<y->x);
}
static void maybe_publish(void)
{
    if(!s_build)return;
    GVDPRasterFrame *f=&s_build->frame;
    if(!f->valid){atom_set(&s_published,-1);return;}
    if(f->row_count!=f->height||s_build->bound_count!=f->height)return;
    int index=(int)(s_build-s_frames);s_active=0;atom_set(&s_build->state,0);
    atom_set(&s_published,index);s_build=NULL;
}
void gvdp_hd_end_row(void)
{
    if(!s_build)return;
    if(!s_active){if(!s_build->frame.valid)atom_set(&s_published,-1);return;}
    GVDPRasterFrame *f=&s_build->frame;GVDPHdRow *r=&f->rows[s_line];
    unsigned count=s_counts[0]+s_counts[1]+s_counts[2];
    if(count>GVDP_HD_SPANS-f->span_count){gvdp_hd_ineligible();atom_set(&s_published,-1);return;}
    qsort(s_s,s_counts[2],sizeof *s_s,compare_span);
    r->a_first=f->span_count;r->a_count=s_counts[0];
    memcpy(f->spans+f->span_count,s_a,(size_t)s_counts[0]*sizeof *s_a);f->span_count+=s_counts[0];
    r->b_first=f->span_count;r->b_count=s_counts[1];
    memcpy(f->spans+f->span_count,s_b,(size_t)s_counts[1]*sizeof *s_b);f->span_count+=s_counts[1];
    r->s_first=f->span_count;r->s_count=s_counts[2];
    memcpy(f->spans+f->span_count,s_s,(size_t)s_counts[2]*sizeof *s_s);f->span_count+=s_counts[2];
    if(!s_build->captured[s_line]){s_build->captured[s_line]=1;++f->row_count;}
    s_active=0;maybe_publish();
}
void gvdp_hd_bind_palette(unsigned line,const uint32_t *argb,unsigned count)
{
    if(!s_build||!s_build->frame.valid||!argb||line>=s_build->frame.height||
       count>GVDP_OUTPUT_PALETTE_SIZE||!s_build->captured[line])return;
    uint32_t colors[GVDP_OUTPUT_PALETTE_SIZE];
    for(unsigned i=0;i<GVDP_OUTPUT_PALETTE_SIZE;++i)colors[i]=i<count?argb[i]:0xff000000u;
    uint64_t key=UINT64_C(1469598103934665603);
    for(unsigned i=0;i<GVDP_OUTPUT_PALETTE_SIZE;++i){key^=colors[i];key*=UINT64_C(1099511628211);}
    int used;unsigned slot=cache_intern(&s_palette_cache,key,colors,&used);
    if(slot==UINT_MAX){gvdp_hd_ineligible();atom_set(&s_published,-1);return;}
    GVDPRasterFrame *f=&s_build->frame;
    if(used){memcpy(f->palette_values[slot],colors,sizeof colors);f->used_palettes[f->palette_count++]=slot;}
    f->rows[line].palette=slot;
    if(!s_build->bound[line]){s_build->bound[line]=1;++s_build->bound_count;}
    maybe_publish();
}
const GVDPRasterFrame *gvdp_hd_acquire(void)
{
    for(;;) {
        /* The acquire-load of a published index also publishes the lazily
         * allocated pool pointer; do not race first enable by reading it first. */
        int i=atom_get(&s_published);if(i<0||i>=HD_FRAME_SLOTS)return NULL;
        HdFrameSlot *slot=&s_frames[i];int n=atom_get(&slot->state);
        if(n<0||n==INT_MAX)continue;
        if(!atom_cas(&slot->state,n,n+1))continue;
        if(atom_get(&s_published)==i&&slot->frame.valid)return &slot->frame;
        gvdp_hd_release(&slot->frame);
    }
}
void gvdp_hd_release(const GVDPRasterFrame *frame)
{
    if(!frame)return;
    if(!s_frames)return;
    for(unsigned i=0;i<HD_FRAME_SLOTS;++i)if(frame==&s_frames[i].frame) {
        int n=atom_get(&s_frames[i].state);
        while(n>0&&!atom_cas(&s_frames[i].state,n,n-1))n=atom_get(&s_frames[i].state);
        return;
    }
}
static void sample(const GVDPRasterFrame *f,unsigned first,unsigned count,unsigned x,
                   int sprite,unsigned *index,unsigned *opaque,unsigned *high)
{
    *index=*opaque=*high=0;unsigned low=0,upper=count;
    while(low<upper){unsigned middle=low+(upper-low)/2;if(f->spans[first+middle].x<=x)low=middle+1;else upper=middle;}
    while(low) {
        const GVDPHdSpan *s=&f->spans[first+--low];unsigned k=x-s->x;
        if(k>7)return;
        if(k<(s->run&255u)&&(!sprite||((s->color>>(16+k))&1u))) {
            unsigned fx=((s->run>>8)&255u)+k;if(s->run&65536u)fx^=7;
            unsigned nib=(f->pattern_values[s->source]>>(28-fx*4))&15;
            if(nib){*index=(s->color&255u)+nib;*opaque=1;*high=(s->color>>8)&1u;return;}
        }
        if(!sprite)return;
    }
}
int gvdp_hd_render_argb(const GVDPRasterFrame *f,uint32_t *out,unsigned stride)
{
    if(!f||!f->valid||!out||!f->width||f->width>GVDP_MAX_WIDTH||
       f->height>GVDP_HD_ROWS||stride<f->width)return 0;
    for(unsigned y=0;y<f->height;++y) {
        const GVDPHdRow *r=&f->rows[y];
        for(unsigned x=0;x<f->width;++x) {
            unsigned pixel=r->bar;
            if(x>=r->start&&x-r->start<r->count) {
                pixel=r->backdrop;
                if(r->display) {
                    unsigned a,ao,ah,b,bo,bh,s,so,sh;
                    sample(f,r->a_first,r->a_count,x,0,&a,&ao,&ah);
                    sample(f,r->b_first,r->b_count,x,0,&b,&bo,&bh);
                    sample(f,r->s_first,r->s_count,x,1,&s,&so,&sh);
                    if(so&&sh)pixel=s;else if(ao&&ah)pixel=a;else if(bo&&bh)pixel=b;
                    else if(so)pixel=s;else if(ao)pixel=a;else if(bo)pixel=b;
                    if(r->shadow_mode&&pixel<64) {
                        unsigned ops=f->operator_words[r->ops_first+x/8],shadow=!((so&&sh)||(ao&&ah)||(bo&&bh)),light=0;
                        if((ops>>(x&7))&1u)shadow=1;
                        if((ops>>(8+(x&7)))&1u){if(shadow)shadow=0;else light=1;}
                        if(light)pixel+=128;else if(shadow)pixel+=64;
                    }
                    if(r->diagnostic&&pixel<GVDP_HOST_PALETTE_BASE&&!((x-r->diag_offset)&r->diag_mask))pixel=(pixel&63)+64;
                }
            }
            out[(size_t)y*stride+x]=f->palette_values[r->palette][pixel&255u];
        }
    }
    return 1;
}

#include "sonic_cd_view.h"
#include "sonic_cd_culls.h"

#include <limits.h>
#include <string.h>

_Static_assert(SEGACD_VIEW_MAX_ENTRIES >= 15 + 2 * SEGACD_CULL_MAX_SITES,
               "View entry capacity must include every qualified cull pair");

/* Local source guards are assembled from the US R11A listing and then checked
 * against every owned MMD. DrawObject starts at R11A $203AF2 (US
 * Object Functions.asm:137), DrawObjects at $203BBC (line 251), SpawnObjects
 * at $20764C, and CheckObjDespawn at $20787E (US Object Spawner.asm lines
 * 42/124/176/385). Absolute JP addresses are never used. */
static const uint8_t draw_object_anchor[] = {
    0x08,0xA8,0x00,0x07,0x00,0x01,0x10,0x28,
    0x00,0x01,0x02,0x40,0x00,0x0C,0x67,0x00
};
static const uint8_t draw_objects_anchor[] = {
    0x45,0xF8,0xF8,0x00,0x7A,0x00,0x49,0xF8,
    0xAC,0x00,0x7E,0x07,0x4A,0x54,0x67,0x00
};
static const uint8_t spawn_anchor[] = {
    0x70,0x00,0x10,0x38,0xF7,0x6C,0x30,0x3B,
    0x00,0x00,0x4E,0xFB,0x00,0x00
};
static const uint8_t spawn_anchor_mask[] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0xFF,0xFF,0x00,0x00
};
static const uint8_t despawn_anchor[] = {
    0x30,0x28,0x00,0x08,0x4A,0x28,0x00,0x01
};

enum {
    FLAG_C=0x0001, FLAG_V=0x0002, FLAG_Z=0x0004,
    FLAG_N=0x0008, FLAG_X=0x0010, FLAG_NZVC=0x000F,
    WORD_SIGN=0x8000
};

typedef enum {
    OP_DRAW_CAMERA_SUB,
    OP_DRAW_RIGHT_CMP,
    OP_SPAWN_SUB_80,
    OP_SPAWN_SUB_80_BASE,
    OP_SPAWN_SUB_80_BACKWARD,
    OP_SPAWN_ADD_300,
    OP_SPAWN_ADD_280,
    OP_SPAWN_SUB_300,
    OP_DESPAWN_SUB_80,
    OP_DESPAWN_CMP_280,
    OP_DRAW_OBJECTS_LEA,
    OP_DRAW_OBJECTS_SAT_STORE,
    OP_SPAWN_MAIN_CMP
} ViewOperation;

enum {
    REFRESH_NONE=0,
    REFRESH_REQUESTED=1,
    REFRESH_BACKWARD=2,
    REFRESH_FORWARD=3
};

static SegaCDView *active_view;

static uint16_t read_be16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }

static int bytes_match(const uint8_t *bytes,const uint8_t *pattern,
                      const uint8_t *mask,size_t length)
{
    for(size_t i=0;i<length;++i)
        if((bytes[i]&(mask?mask[i]:0xFFu)) !=
           (pattern[i]&(mask?mask[i]:0xFFu))) return 0;
    return 1;
}

static int find_unique(const uint8_t *image,size_t image_size,
                       size_t begin,size_t end,const uint8_t *pattern,
                       const uint8_t *mask,size_t length,size_t *found)
{
    size_t count=0,hit=0;
    if(!image||!pattern||!found||begin>end||end>image_size||length>end-begin)
        return 0;
    for(size_t i=begin;i<=end-length;++i) {
        if(bytes_match(image+i,pattern,mask,length)) { hit=i; ++count; }
    }
    if(count!=1)return 0;
    *found=hit;
    return 1;
}

static size_t find_all(const uint8_t *image,size_t begin,size_t end,
                       const uint8_t *pattern,size_t length,
                       size_t *hits,size_t capacity)
{
    size_t count=0;
    if(!image||begin>end||length>end-begin)return 0;
    for(size_t i=begin;i<=end-length;++i) {
        if(memcmp(image+i,pattern,length)==0) {
            if(count<capacity)hits[count]=i;
            ++count;
        }
    }
    return count;
}

static int object_layout_proof(const uint8_t *image,size_t size,
                               uint32_t base,size_t spawn,
                               uint32_t *layout_offset_out,
                               uint32_t *first_offset_out)
{
    size_t lea=spawn+0x16u;
    uint32_t layout_address,first_address;
    int16_t first_delta;
    size_t layout_offset,first_offset;
    if(lea>size||6u>size-lea||image[lea]!=0x41u||image[lea+1u]!=0xF9u)
        return 0;
    layout_address=((uint32_t)image[lea+2u]<<24)|
                   ((uint32_t)image[lea+3u]<<16)|
                   ((uint32_t)image[lea+4u]<<8)|image[lea+5u];
    if(layout_address<base||
       (uint64_t)layout_address-base+2u>size)return 0;
    layout_offset=(size_t)(layout_address-base);
    first_delta=(int16_t)read_be16(image+layout_offset);
    if((int64_t)layout_address+first_delta<base||
       (int64_t)layout_address+first_delta>=(int64_t)base+(int64_t)size)
        return 0;
    first_address=(uint32_t)((int64_t)layout_address+first_delta);
    first_offset=(size_t)(first_address-base);
    /* oeSize is eight bytes. The source walks backward by one record and
     * compares the predecessor's oeX; the first record has a -1 sentinel. */
    if(first_offset<8u||first_offset+8u>size||
       read_be16(image+first_offset-8u)!=0xFFFFu)return 0;
    *layout_offset_out=(uint32_t)layout_offset;
    *first_offset_out=(uint32_t)first_offset;
    return 1;
}

static int spawn_layout_still_valid(const SegaCDView *view)
{
    uint32_t layout_offset,first_offset;
    if(!view||!view->guard_image||
       view->spawn_anchor_offset>view->guard_image_size||
       sizeof spawn_anchor>view->guard_image_size-view->spawn_anchor_offset||
       !bytes_match(view->guard_image+view->spawn_anchor_offset,spawn_anchor,
                    spawn_anchor_mask,sizeof spawn_anchor) ||
       !object_layout_proof(view->guard_image,view->guard_image_size,
                            view->image_base,view->spawn_anchor_offset,
                            &layout_offset,&first_offset))
        return 0;
    return layout_offset==view->object_layout_offset&&
           first_offset==view->first_object_offset;
}

static int ring_cull_sites(const SegaCDViewSnapshot *snapshot,
                            size_t *sub_out,size_t *cmp_out)
{
    const uint8_t *image=snapshot->word_ram;
    size_t size=snapshot->word_ram_size,run,table,ring,states,main;
    uint32_t address;
    int64_t target;
    static const uint8_t runner[]={0xD0,0x40,0xD0,0x40,0x43,0xF9};
    static const uint8_t dispatch[]={0x70,0x00,0x10,0x28,0x00,0x24,
                                    0x32,0x3B,0x00,0x06,0x4E,0xFB,0x10,0x02};
    static const uint8_t cull[]={0x4A,0x28,0x00,0x01,0x6B,0x1E,
        0x30,0x28,0x00,0x32,0x02,0x40,0xFF,0x80,0x32,0x38,0xF7,0x00,
        0x04,0x41,0x00,0x80,0x02,0x41,0xFF,0x80,0x90,0x41,
        0x0C,0x40,0x02,0x80,0x62,0x00};
    /* RunObjects indexes ObjectIndex by (ID-1)*4. Bind specifically to
     * ring ID $10 and its routine-2 state: other objects contain the same
     * inline bounds sequence and must not be mistaken for the ring code. */
    if(!find_unique(image,size,0,size,runner,NULL,sizeof runner,&run) ||
       run>size || 16u>size-run ||
       memcmp(image+run+10u,(const uint8_t[]){0x22,0x71,0x00,0xFC,0x4E,0x91},6))
        return 0;
    address=((uint32_t)image[run+6u]<<24)|((uint32_t)image[run+7u]<<16)|
            ((uint32_t)image[run+8u]<<8)|image[run+9u];
    if(address<snapshot->word_ram_base ||
       (uint64_t)address-snapshot->word_ram_base+64u>size)return 0;
    table=address-snapshot->word_ram_base;
    address=((uint32_t)image[table+60u]<<24)|((uint32_t)image[table+61u]<<16)|
            ((uint32_t)image[table+62u]<<8)|image[table+63u];
    if(address<snapshot->word_ram_base ||
       (uint64_t)address-snapshot->word_ram_base+sizeof dispatch>size)return 0;
    ring=address-snapshot->word_ram_base;
    if(memcmp(image+ring,dispatch,sizeof dispatch))return 0;
    states=ring+14u;
    if(states>size || 4u>size-states)return 0;
    target=(int64_t)states+(int16_t)read_be16(image+states+2u);
    if(target<0 || (uint64_t)target+sizeof cull>size)return 0;
    main=(size_t)target;
    if(memcmp(image+main,cull,sizeof cull))return 0;
    *sub_out=main+18u;*cmp_out=main+28u;
    return 1;
}

static void set_sub_flags(M68kStepContext *ctx,uint16_t dst,uint16_t src,
                          uint16_t result,int update_x)
{
    uint16_t flags=(uint16_t)(ctx->state.regs.SR&~FLAG_NZVC);
    if(result&WORD_SIGN)flags|=FLAG_N;
    if(!result)flags|=FLAG_Z;
    if(((dst^src)&(dst^result)&WORD_SIGN)!=0)flags|=FLAG_V;
    if((uint32_t)src>(uint32_t)dst)flags|=FLAG_C;
    if(update_x) {
        flags=(uint16_t)(flags&~FLAG_X);
        if(flags&FLAG_C)flags|=FLAG_X;
    }
    ctx->state.regs.SR=flags;
}

static void set_add_flags(M68kStepContext *ctx,uint16_t dst,uint16_t src,
                          uint16_t result)
{
    uint16_t flags=(uint16_t)(ctx->state.regs.SR&~(FLAG_NZVC|FLAG_X));
    if(result&WORD_SIGN)flags|=FLAG_N;
    if(!result)flags|=FLAG_Z;
    if(((~(dst^src))&(dst^result)&WORD_SIGN)!=0)flags|=FLAG_V;
    if((uint32_t)dst+(uint32_t)src>0xFFFFu)flags|=FLAG_C|FLAG_X;
    ctx->state.regs.SR=flags;
}

static void subtract_dreg(M68kStepContext *ctx,unsigned dreg,uint16_t source,
                          int update_x)
{
    uint16_t dst=(uint16_t)ctx->state.regs.D[dreg];
    uint16_t result=(uint16_t)(dst-source);
    ctx->state.regs.D[dreg]=(ctx->state.regs.D[dreg]&0xFFFF0000u)|result;
    set_sub_flags(ctx,dst,source,result,update_x);
}

static void compare_dreg(M68kStepContext *ctx,unsigned dreg,uint16_t source)
{
    uint16_t dst=(uint16_t)ctx->state.regs.D[dreg];
    set_sub_flags(ctx,dst,source,(uint16_t)(dst-source),0);
}

static void force_compare_flags(M68kStepContext *ctx,int backward)
{
    uint16_t flags=(uint16_t)(ctx->state.regs.SR&~FLAG_NZVC);
    if(backward)flags|=FLAG_N;
    ctx->state.regs.SR=flags;
}

static void cancel_spawn_refresh(SegaCDView *view)
{
    view->refresh_phase=REFRESH_NONE;
    view->refresh_underflow_clamped=0;
    view->refresh_chunk=0;
    view->refresh_underflow_result=0;
}

static void add_dreg(M68kStepContext *ctx,unsigned dreg,uint16_t source)
{
    uint16_t dst=(uint16_t)ctx->state.regs.D[dreg];
    uint32_t sum=(uint32_t)dst+source;
    uint16_t result=(uint16_t)sum;
    ctx->state.regs.D[dreg]=(ctx->state.regs.D[dreg]&0xFFFF0000u)|result;
    set_add_flags(ctx,dst,source,result);
}

static int current_source(const SegaCDView *view, uint32_t pc)
{
    for (size_t n = 0; n < view->module.count; ++n) {
        if (view->entries[n].pc != pc) continue;
        size_t at = view->guard_offset[n], length = view->guard_length[n];
        return view->guard_image && at <= view->guard_image_size &&
            length <= view->guard_image_size - at &&
            memcmp(view->guard_image + at, view->guard_bytes[n], length) == 0;
    }
    return 0;
}

static void view_operation(M68kStepContext *ctx,ViewOperation operation)
{
    SegaCDView *view=active_view;
    uint16_t amount,source;
    if(!view) { m68k_step_unimplemented(ctx); return; }
    int qualified = current_source(view, ctx->state.instruction_pc);
    if(!qualified && view->refresh_phase!=REFRESH_NONE) {
        if(view->refresh_underflow_clamped)
            ctx->state.regs.D[6]=(ctx->state.regs.D[6]&0xFFFF0000u)|
                                  view->refresh_underflow_result;
        cancel_spawn_refresh(view);
    }
    switch(operation) {
    case OP_DRAW_CAMERA_SUB:
        /* Original SUB.W cameraX.w,D3; subtract the left-shifted camera to
         * translate source coordinates by the true symmetric pixel margin. */
        /* Absolute short $F700 sign-extends in A7-space, then the 24-bit
         * machine bus masks it to $FFF700. */
        source=m68k_step_read16(ctx,0x00FFF700u);
        if(ctx->state.fault)return;
        if (qualified) source=(uint16_t)(source-view->pixel_margin);
        subtract_dreg(ctx,3,source,1);
        break;
    case OP_DRAW_RIGHT_CMP:
        compare_dreg(ctx,1,qualified ? view->effective_width : 320u);
        break;
    case OP_SPAWN_SUB_80:
    case OP_DESPAWN_SUB_80:
        amount=(uint16_t)(0x0080u+(qualified ? view->chunk_margin : 0u));
        subtract_dreg(ctx,operation==OP_SPAWN_SUB_80?6u:1u,amount,1);
        break;
    case OP_SPAWN_SUB_80_BACKWARD: {
        uint16_t before=(uint16_t)ctx->state.regs.D[6];
        amount=(uint16_t)(0x0080u+(qualified?view->chunk_margin:0u));
        subtract_dreg(ctx,6,amount,1);
        if(qualified && view->refresh_phase==REFRESH_BACKWARD &&
           before<amount) {
            view->refresh_underflow_result=(uint16_t)ctx->state.regs.D[6];
            view->refresh_underflow_clamped=1;
            ctx->state.regs.D[6]=(ctx->state.regs.D[6]&0xFFFF0000u)|0xFFFFu;
            ctx->state.regs.SR=(uint16_t)(ctx->state.regs.SR&~FLAG_C);
        }
        break;
    }
    case OP_SPAWN_SUB_80_BASE:
        subtract_dreg(ctx,6,0x0080u,1);
        break;
    case OP_SPAWN_ADD_300:
    case OP_SPAWN_ADD_280:
        if(operation==OP_SPAWN_ADD_300 && qualified &&
           view->refresh_underflow_clamped) {
            ctx->state.regs.D[6]=(ctx->state.regs.D[6]&0xFFFF0000u)|
                                  view->refresh_underflow_result;
            view->refresh_underflow_clamped=0;
        }
        amount=(uint16_t)((operation==OP_SPAWN_ADD_300?0x0300u:0x0280u)+
                          (!qualified ? 0u : operation==OP_SPAWN_ADD_300?
                           2u*view->chunk_margin:view->chunk_margin));
        add_dreg(ctx,6,amount);
        break;
    case OP_SPAWN_SUB_300:
        amount=(uint16_t)(0x0300u+(qualified ? 2u*view->chunk_margin : 0u));
        subtract_dreg(ctx,6,amount,1);
        if(qualified&&view->refresh_phase==REFRESH_FORWARD)
            cancel_spawn_refresh(view);
        break;
    case OP_DESPAWN_CMP_280:
        compare_dreg(ctx,0,(uint16_t)(0x0280u+(qualified ? 2u*view->chunk_margin : 0u)));
        break;
    case OP_DRAW_OBJECTS_LEA:
        /* Preserve the guest LEA's sign-extended absolute-word result. */
        ctx->state.regs.A[2]=0xFFFFF800u;
        if(qualified&&view->queue_capture)
            view->queue_capture(view->queue_capture_user,ctx);
        break;
    case OP_DRAW_OBJECTS_SAT_STORE: {
        uint8_t value=(uint8_t)ctx->state.regs.D[5];
        uint16_t flags=(uint16_t)(ctx->state.regs.SR&~FLAG_NZVC);
        if(qualified&&view->sat_capture)view->sat_capture(view->sat_capture_user,ctx);
        m68k_step_write8(ctx,0x00FFF62Cu,value);
        if(ctx->state.fault)return;
        if(value&0x80u)flags|=FLAG_N;
        if(value==0)flags|=FLAG_Z;
        ctx->state.regs.SR=flags;
        break;
    }
    case OP_SPAWN_MAIN_CMP: {
        uint16_t previous;
        uint8_t routine;
        if(qualified && view->refresh_phase!=REFRESH_NONE &&
           !spawn_layout_still_valid(view)) {
            cancel_spawn_refresh(view);
            qualified=0;
        }
        previous=m68k_step_read16(ctx,0x00FFF76Eu);
        if(ctx->state.fault)break;
        compare_dreg(ctx,6,previous);
        if(!qualified || view->refresh_phase==REFRESH_NONE)break;
        routine=m68k_step_read8(ctx,0x00FFF76Cu);
        if(ctx->state.fault) {cancel_spawn_refresh(view);break;}
        if(routine!=2u) {cancel_spawn_refresh(view);break;}
        if(view->refresh_phase==REFRESH_REQUESTED) {
            view->refresh_chunk=(uint16_t)ctx->state.regs.D[6];
            view->refresh_phase=REFRESH_BACKWARD;
            force_compare_flags(ctx,1);
        } else if(view->refresh_phase==REFRESH_FORWARD) {
            uint16_t chunk=(uint16_t)ctx->state.regs.D[6];
            if(chunk!=view->refresh_chunk) {
                view->refresh_chunk=chunk;
                view->refresh_phase=REFRESH_BACKWARD;
                force_compare_flags(ctx,1);
            } else {
                force_compare_flags(ctx,0);
            }
        } else if(view->refresh_phase==REFRESH_BACKWARD) {
            uint16_t chunk=(uint16_t)ctx->state.regs.D[6];
            if(chunk!=view->refresh_chunk) {
                view->refresh_chunk=chunk;
                force_compare_flags(ctx,1);
            } else {
                view->refresh_phase=REFRESH_FORWARD;
                force_compare_flags(ctx,0);
            }
        }
        break;
    }
    default:
        m68k_step_unimplemented(ctx);
        break;
    }
}

#define VIEW_OP(name,op) \
    static void name(M68kStepContext *ctx){view_operation(ctx,op);}
VIEW_OP(op_draw_camera_sub,OP_DRAW_CAMERA_SUB)
VIEW_OP(op_draw_right_cmp,OP_DRAW_RIGHT_CMP)
VIEW_OP(op_spawn_sub_80,OP_SPAWN_SUB_80)
VIEW_OP(op_spawn_sub_80_base,OP_SPAWN_SUB_80_BASE)
VIEW_OP(op_spawn_sub_80_backward,OP_SPAWN_SUB_80_BACKWARD)
VIEW_OP(op_spawn_add_300,OP_SPAWN_ADD_300)
VIEW_OP(op_spawn_add_280,OP_SPAWN_ADD_280)
VIEW_OP(op_spawn_sub_300,OP_SPAWN_SUB_300)
VIEW_OP(op_despawn_sub_80,OP_DESPAWN_SUB_80)
VIEW_OP(op_despawn_cmp_280,OP_DESPAWN_CMP_280)
VIEW_OP(op_draw_objects_lea,OP_DRAW_OBJECTS_LEA)
VIEW_OP(op_draw_objects_sat_store,OP_DRAW_OBJECTS_SAT_STORE)
VIEW_OP(op_spawn_main_cmp,OP_SPAWN_MAIN_CMP)
#undef VIEW_OP

static M68kStepOperation operation_callback(ViewOperation op)
{
    switch(op) {
    case OP_DRAW_CAMERA_SUB:return op_draw_camera_sub;
    case OP_DRAW_RIGHT_CMP:return op_draw_right_cmp;
    case OP_SPAWN_SUB_80:return op_spawn_sub_80;
    case OP_SPAWN_SUB_80_BASE:return op_spawn_sub_80_base;
    case OP_SPAWN_SUB_80_BACKWARD:return op_spawn_sub_80_backward;
    case OP_SPAWN_ADD_300:return op_spawn_add_300;
    case OP_SPAWN_ADD_280:return op_spawn_add_280;
    case OP_SPAWN_SUB_300:return op_spawn_sub_300;
    case OP_DESPAWN_SUB_80:return op_despawn_sub_80;
    case OP_DESPAWN_CMP_280:return op_despawn_cmp_280;
    case OP_DRAW_OBJECTS_LEA:return op_draw_objects_lea;
    case OP_DRAW_OBJECTS_SAT_STORE:return op_draw_objects_sat_store;
    case OP_SPAWN_MAIN_CMP:return op_spawn_main_cmp;
    default:return NULL;
    }
}

static int append_entry(M68kStepEntry *entries,size_t *count,size_t capacity,
                        const SegaCDViewSnapshot *snapshot,size_t offset,
                        ViewOperation operation,unsigned cycles)
{
    M68kStepEntry *entry;
    if(*count>=capacity||offset+4u>snapshot->word_ram_size||
       ((snapshot->word_ram_base+(uint32_t)offset)&1u))return 0;
    entry=&entries[(*count)++];
    memset(entry,0,sizeof *entry);
    entry->pc=snapshot->word_ram_base+(uint32_t)offset;
    entry->words[0]=read_be16(snapshot->word_ram+offset);
    entry->words[1]=read_be16(snapshot->word_ram+offset+2u);
    entry->word_count=2;
    entry->cycles=(uint16_t)cycles;
    entry->execute=operation_callback(operation);
    return entry->execute!=NULL;
}

static void sort_entries(M68kStepEntry *entries,size_t count)
{
    for(size_t i=1;i<count;++i) {
        M68kStepEntry item=entries[i];
        size_t j=i;
        while(j&&entries[j-1].pc>item.pc) { entries[j]=entries[j-1];--j; }
        entries[j]=item;
    }
}

void segacd_view_init(SegaCDView *view)
{
    if(!view)return;
    memset(view,0,sizeof *view);
}

static void clear_configuration(SegaCDView *view)
{
    memset(&view->module,0,sizeof view->module);
    memset(view->entries,0,sizeof view->entries);
    view->guard_image=NULL;view->guard_image_size=0;
    memset(view->guard_length,0,sizeof view->guard_length);
    view->spawn_anchor_offset=0;
    view->object_layout_offset=0;
    view->first_object_offset=0;
    view->queue_capture=NULL;
    view->queue_capture_user=NULL;
    view->sat_capture=NULL;
    view->sat_capture_user=NULL;
    view->image_base=view->image_size=0;
    view->requested_width=view->effective_width=0;
    view->pixel_margin=view->chunk_margin=0;
    cancel_spawn_refresh(view);
    view->configured=0;
}

static int valid_snapshot(const SegaCDViewSnapshot *s)
{
    uint64_t word_end,main_end;
    if(!s||!s->word_ram||!s->main_ram||s->word_ram_size!=0x40000u||
       s->main_ram_size<0x10000u)return 0;
    word_end=(uint64_t)s->word_ram_base+s->word_ram_size;
    main_end=(uint64_t)s->main_ram_base+s->main_ram_size;
    if((s->word_ram_base&1u)||word_end>0x1000000u||main_end>0x1000000u)
        return 0;
    if(s->word_ram_base>0xFFFFFFu||s->word_ram_base+0x40000u>0x1000000u)
        return 0;
    if(0x00FFF700u<s->main_ram_base||0x00FFF702u>=main_end)
        return 0;
    return 1;
}

static int discover_sites(const SegaCDViewSnapshot *s,M68kStepEntry *entries,
                          size_t *entry_count,
                          const SegaCDViewObservers *observers,
                          uint32_t *spawn_anchor_offset_out,
                          uint32_t *object_layout_offset_out,
                          uint32_t *first_object_offset_out)
{
    const uint8_t *image=s->word_ram;
    size_t size=s->word_ram_size,draw,draw_objects,spawn,despawn;
    size_t sub_camera,cmp_width,despawn_sub,despawn_cmp,spawn_main_cmp;
    size_t ring_sub,ring_cmp;
    size_t lefts[4],count,add300,add280,sub300;
    uint32_t object_layout_offset,first_object_offset;
    SegaCDCullDiscovery culls;
    M68kStepEntry temp[SEGACD_VIEW_MAX_ENTRIES];
    size_t n=0;

    if(!find_unique(image,size,0,size,draw_object_anchor,NULL,
                    sizeof draw_object_anchor,&draw) ||
       !find_unique(image,size,0,size,draw_objects_anchor,NULL,
                    sizeof draw_objects_anchor,&draw_objects) ||
       !find_unique(image,size,0,size,spawn_anchor,spawn_anchor_mask,
                    sizeof spawn_anchor,&spawn) ||
       !find_unique(image,size,0,size,despawn_anchor,NULL,
                    sizeof despawn_anchor,&despawn))return 0;
    if(!(draw<draw_objects&&spawn<despawn))return 0;
    if(!find_unique(image,size,spawn,despawn,
                    (const uint8_t[]){0xBC,0x78,0xF7,0x6E},NULL,4,
                    &spawn_main_cmp) ||
       !object_layout_proof(image,size,s->word_ram_base,spawn,
                            &object_layout_offset,&first_object_offset))return 0;

    /* DrawObject source order is horizontal camera subtraction followed by
     * width-adjusted left and right edges. Require each complete opcode in
     * the uniquely anchored function window. */
    if(!find_unique(image,size,draw,draw+0x100u,
                    (const uint8_t[]){0x96,0x78,0xF7,0x00},NULL,4,&sub_camera) ||
       !find_unique(image,size,draw,draw+0x100u,
                    (const uint8_t[]){0x0C,0x41,0x01,0x40},NULL,4,&cmp_width) ||
       !(sub_camera<cmp_width))return 0;

    /* The scheduler's source order has three #$80 subtracts, followed by
     * paired forward/backward cursor adjustments and right horizons. */
    count=find_all(image,spawn,despawn,
                   (const uint8_t[]){0x04,0x46,0x00,0x80},4,
                   lefts,sizeof lefts/sizeof lefts[0]);
    if(count!=3)return 0;
    if(!find_unique(image,size,spawn,despawn,
                    (const uint8_t[]){0x06,0x46,0x03,0x00},NULL,4,&add300) ||
       !find_unique(image,size,spawn,despawn,
                    (const uint8_t[]){0x06,0x46,0x02,0x80},NULL,4,&add280) ||
       !find_unique(image,size,spawn,despawn,
                    (const uint8_t[]){0x04,0x46,0x03,0x00},NULL,4,&sub300) ||
       !(lefts[0]<lefts[1]&&lefts[1]<lefts[2]&&
         lefts[2]<add300&&add300<add280&&add280<sub300))return 0;

    if(!find_unique(image,size,despawn,despawn+0x100u,
                    (const uint8_t[]){0x04,0x41,0x00,0x80},NULL,4,&despawn_sub) ||
       !find_unique(image,size,despawn,despawn+0x100u,
                    (const uint8_t[]){0x0C,0x40,0x02,0x80},NULL,4,&despawn_cmp) ||
       !(despawn_sub<despawn_cmp) ||
       !ring_cull_sites(s,&ring_sub,&ring_cmp))return 0;

    /* Entries retain original opcode/extension guards. Each handler changes
     * only the guest arithmetic operand, not the instruction's cycle cost or
     * PC width. Cycles are the nominal MC68000 PRM values for these forms:
     * SUB abs.W,Dn=12; LEA abs.W,An=8; immediate word Dn=8. */
    if(!append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,sub_camera,
                     OP_DRAW_CAMERA_SUB,12) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,cmp_width,
                     OP_DRAW_RIGHT_CMP,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,lefts[0],
                     OP_SPAWN_SUB_80,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,lefts[1],
                     OP_SPAWN_SUB_80_BASE,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,lefts[2],
                     OP_SPAWN_SUB_80_BACKWARD,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,add300,
                     OP_SPAWN_ADD_300,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,add280,
                     OP_SPAWN_ADD_280,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,sub300,
                     OP_SPAWN_SUB_300,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,despawn_sub,
                     OP_DESPAWN_SUB_80,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,despawn_cmp,
                     OP_DESPAWN_CMP_280,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,ring_sub,
                     OP_DESPAWN_SUB_80,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,ring_cmp,
                     OP_DESPAWN_CMP_280,8) ||
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,spawn_main_cmp,
                     OP_SPAWN_MAIN_CMP,12))return 0;
    if(!segacd_culls_discover(s,&culls))return 0;
    for(size_t i=0;i<culls.site_count;++i) {
        const SegaCDCullSite *site=&culls.sites[i];
        if(site->camera_register!=1u || site->object_register!=0u ||
           site->subtract_pc<s->word_ram_base || site->compare_pc<s->word_ram_base ||
           !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,
                         site->subtract_pc-s->word_ram_base,OP_DESPAWN_SUB_80,8) ||
           !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,
                         site->compare_pc-s->word_ram_base,OP_DESPAWN_CMP_280,8))return 0;
    }

    if(observers && observers->queue_capture &&
       !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,draw_objects,
                     OP_DRAW_OBJECTS_LEA,8))return 0;
    if(observers && observers->sat_capture) {
        size_t sat_store;
        static const uint8_t sat_sequence[]={0x11,0xC5,0xF6,0x2C,
                                             0x0C,0x05,0x00,0x50};
        if(!find_unique(image,size,draw_objects,spawn,sat_sequence,NULL,
                        sizeof sat_sequence,&sat_store) ||
           !append_entry(temp,&n,SEGACD_VIEW_MAX_ENTRIES,s,sat_store,
                         OP_DRAW_OBJECTS_SAT_STORE,12))return 0;
    }
    sort_entries(temp,n);
    for(size_t i=1;i<n;++i)if(temp[i-1].pc>=temp[i].pc)return 0;
    memcpy(entries,temp,n*sizeof *entries);
    *entry_count=n;
    *spawn_anchor_offset_out=(uint32_t)spawn;
    *object_layout_offset_out=object_layout_offset;
    *first_object_offset_out=first_object_offset;
    return 1;
}

int segacd_view_configure(SegaCDView *view,const SegaCDViewSnapshot *snapshot,
                          unsigned requested_width,
                          SegaCDViewQueueCapture capture,void *capture_user)
{
    SegaCDViewObservers observers;
    memset(&observers,0,sizeof observers);
    observers.queue_capture=capture;
    observers.queue_capture_user=capture_user;
    return segacd_view_configure_observed(view,snapshot,requested_width,
                                          &observers);
}

int segacd_view_configure_observed(SegaCDView *view,
                          const SegaCDViewSnapshot *snapshot,
                          unsigned requested_width,
                          const SegaCDViewObservers *observers)
{
    M68kStepEntry entries[SEGACD_VIEW_MAX_ENTRIES];
    size_t entry_count=0;
    unsigned margin,chunk_margin,effective_width;
    uint32_t spawn_anchor_offset=0,object_layout_offset=0,first_object_offset=0;
    if(!view||view->installed)return 0;
    clear_configuration(view);
    if(!valid_snapshot(snapshot)||requested_width<320u||
       requested_width>SEGACD_SONIC_CD_MAX_VIEW_WIDTH)
        return 0;
    margin=(requested_width-320u)/2u;
    effective_width=320u+2u*margin;
    chunk_margin=((margin+127u)/128u)*128u;
    if(effective_width>32767u||0x0300u+2u*chunk_margin>32767u)return 0;
    if(effective_width>320u) {
        memset(entries,0,sizeof entries);
        if(!discover_sites(snapshot,entries,&entry_count,observers,
                           &spawn_anchor_offset,&object_layout_offset,
                           &first_object_offset))return 0;
        memcpy(view->entries,entries,entry_count*sizeof *entries);
        view->guard_image=snapshot->word_ram;
        view->guard_image_size=snapshot->word_ram_size;
        view->spawn_anchor_offset=spawn_anchor_offset;
        view->object_layout_offset=object_layout_offset;
        view->first_object_offset=first_object_offset;
        for (size_t n=0;n<entry_count;++n) {
            size_t at=entries[n].pc-snapshot->word_ram_base;
            at=at>=12u ? at-12u : 0u;
            size_t length=snapshot->word_ram_size-at;
            if(length>32u)length=32u;
            view->guard_offset[n]=(uint32_t)at;
            view->guard_length[n]=(uint8_t)length;
            memcpy(view->guard_bytes[n],snapshot->word_ram+at,length);
        }
    }
    view->requested_width=(uint16_t)requested_width;
    view->effective_width=(uint16_t)effective_width;
    view->pixel_margin=(uint16_t)margin;
    view->chunk_margin=(uint16_t)chunk_margin;
    if(observers) {
        view->queue_capture=observers->queue_capture;
        view->queue_capture_user=observers->queue_capture_user;
        view->sat_capture=observers->sat_capture;
        view->sat_capture_user=observers->sat_capture_user;
    }
    view->image_base=snapshot->word_ram_base;
    view->image_size=(uint32_t)snapshot->word_ram_size;
    view->module.name="Sonic CD source-guarded horizontal bounds";
    view->module.image_base=view->image_base;
    view->module.image_size=view->image_size;
    view->module.image_crc32=0;
    view->module.entries=view->entries;
    view->module.count=entry_count;
    view->configured=1;
    return 1;
}

const M68kStepModule *segacd_view_module(const SegaCDView *view)
{ return view&&view->configured?&view->module:NULL; }

int segacd_view_mark_installed(SegaCDView *view,int installed)
{
    if(!view||!view->configured)return 0;
    if(installed) {
        if(!view->module.count||(active_view&&active_view!=view))return 0;
        active_view=view;
        view->installed=1;
    } else {
        if(active_view==view)active_view=NULL;
        view->installed=0;
    }
    return 1;
}

int segacd_view_request_spawn_refresh(SegaCDView *view,
                                      const SegaCDViewSnapshot *snapshot)
{
    if(!view||!view->configured||view->installed||!snapshot||
       !valid_snapshot(snapshot)||snapshot->word_ram!=view->guard_image||
       snapshot->word_ram_base!=view->image_base||
       snapshot->word_ram_size!=view->image_size||
       snapshot->main_ram_base!=0x00FF0000u||
       snapshot->main_ram_size<0x10000u||
       snapshot->main_ram[0xF76Cu]!=2u||!spawn_layout_still_valid(view))
        return 0;
    cancel_spawn_refresh(view);
    view->refresh_phase=REFRESH_REQUESTED;
    return 1;
}

int segacd_view_spawn_refresh_pending(const SegaCDView *view)
{ return view&&view->refresh_phase!=REFRESH_NONE; }

#include "sonic_cd_culls.h"

#include <string.h>

/* These are full source instruction windows, not immediate searches. Object
 * handlers are rooted through RunObjects -> ObjectIndex and their local
 * indexed state tables; branch extensions are decoded and checked against
 * the corresponding source cleanup/draw target. */
static const uint8_t run_objects[] = {
    0xD0,0x40,0xD0,0x40,0x43,0xF9
};
static const uint8_t run_objects_tail[] = {
    0x22,0x71,0x00,0xFC,0x4E,0x91
};
static const uint8_t dispatcher[] = {
    0x70,0x00,0x10,0x28,0x00,0x24,0x30,0x3B
};
static const uint8_t test_badnik_cull[] = {
    0x30,0x28,0x00,0x30,0x02,0x40,0xFF,0x80,
    0x32,0x38,0xF7,0x00,0x04,0x41,0x00,0x80,
    0x02,0x41,0xFF,0x80,0x90,0x41,0x0C,0x40,
    0x02,0x80,0x62,0x00
};
static const uint8_t float_block_cull[] = {
    0x30,0x28,0x00,0x08,0x02,0x40,0xFF,0x80,
    0x32,0x38,0xF7,0x00,0x04,0x41,0x00,0x80,
    0x02,0x41,0xFF,0x80,0x90,0x41,0x0C,0x40,
    0x02,0x80,0x62,0x00
};
static const uint8_t moving_spring_cull[] = {
    0x30,0x28,0x00,0x36,0x02,0x40,0xFF,0x80,
    0x32,0x38,0xF7,0x00,0x04,0x41,0x00,0x80,
    0x02,0x41,0xFF,0x80,0x90,0x41,0x0C,0x40,
    0x02,0x80,0x62,0x00
};
static const uint8_t s1_cull_prefix[] = {
    0x30,0x28,0x00,0x08,0x02,0x40,0xFF,0x80,
    0x32,0x38,0xF7,0x00,0x04,0x41,0x00,0x80,
    0x02,0x41,0xFF,0x80,0x90,0x41,0x0C,0x40,
    0x02,0x80,0x62,0x00
};
static const uint8_t draw_object_anchor[] = {
    0x08,0xA8,0x00,0x07,0x00,0x01,0x10,0x28,
    0x00,0x01,0x02,0x40,0x00,0x0C,0x67,0x00
};
static const uint8_t delete_object_anchor[] = {
    0x22,0x48,0x72,0x00,0x70,0x0F,0x22,0xC1,
    0x51,0xC8,0xFF,0xFC,0x4E,0x75
};
/* Object Spawner.asm: DespawnObject's saved-flag path followed by its
 * absolute transfer to the owned DeleteObject routine. The transfer address
 * is checked separately against the unique function anchor. */
static const uint8_t despawn_saved_flags_prefix[] = {
    0x70,0x00,0x10,0x28,0x00,0x23,0x67,0x1C,
    0x43,0xF9,0x00,0xFF,0x12,0x00,0x32,0x00,
    0xD2,0x41,0xD0,0x41,0x72,0x00,0x12,0x39,
    0x00,0xFF,0x15,0x2E,0xD0,0x41,0x08,0xB1,
    0x00,0x07,0x00,0x02
};
static const uint8_t get_off_object_anchor[] = {
    0x08,0x28,0x00,0x03,0x00,0x22,0x67,0x68
};
static const uint8_t unqualified_r43c_table_prefix[] = {
    0xF7,0x00,0x00,0xFF,0xF7,0x08,0x00,0xFF,
    0xF7,0x18,0x45,0xF8,0xF8,0x00,0x7A,0x00
};
static const uint8_t s1_cleanup[] = {
    0x45,0xF9,0x00,0xFF,0x12,0x00,0x70,0x00,
    0x10,0x28,0x00,0x23,0x67,0x06,
    0x08,0xB2,0x00,0x07,0x00,0x02,0x60,0x00
};

static uint16_t read_be16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}

static int in_range(size_t size,size_t offset,size_t length)
{ return offset<=size&&length<=size-offset; }

static int find_unique(const uint8_t *image,size_t size,size_t begin,size_t end,
                       const uint8_t *pattern,size_t length,size_t *found)
{
    size_t count=0,hit=0;
    if(!image||!pattern||!found||begin>end||end>size||length>end-begin)
        return 0;
    for(size_t i=begin;i<=end-length;++i) {
        if(memcmp(image+i,pattern,length)==0) { hit=i; ++count; }
    }
    if(count!=1u)return 0;
    *found=hit;
    return 1;
}

static int branch_word_target(const uint8_t *image,size_t size,
                              size_t opcode_offset,uint16_t opcode,
                              size_t *target_out)
{
    int16_t displacement;
    int64_t target;
    if(!in_range(size,opcode_offset,4u)||
       read_be16(image+opcode_offset)!=opcode)return 0;
    displacement=(int16_t)read_be16(image+opcode_offset+2u);
    /* 68000 word branches use the address after the opcode word as base. */
    target=(int64_t)opcode_offset+2+displacement;
    if(target<0||target>=(int64_t)size||(target&1))return 0;
    *target_out=(size_t)target;
    return 1;
}

static int source_pc(const SegaCDViewSnapshot *s,uint32_t address,
                     size_t length,size_t *offset)
{
    uint64_t delta;
    if(!s||(address&1u)||address<s->word_ram_base)return 0;
    delta=(uint64_t)address-s->word_ram_base;
    if(delta>s->word_ram_size||length>s->word_ram_size-(size_t)delta)
        return 0;
    *offset=(size_t)delta;
    return 1;
}

/* Decode a source PC-indexed word table. The extension's high nibble is the
 * index register; the low byte is the signed PC displacement. All table
 * entries are source-relative words and must resolve to aligned owned code. */
static int indexed_word_table(const uint8_t *image,size_t size,size_t opcode,
                              uint16_t expected_opcode,unsigned index_reg,
                              size_t item_count,size_t *table_out,
                              size_t targets[16])
{
    uint16_t extension;
    int8_t displacement;
    int64_t table;
    if(item_count>16u||!in_range(size,opcode,4u)||
       read_be16(image+opcode)!=expected_opcode)return 0;
    extension=read_be16(image+opcode+2u);
    if((unsigned)(extension>>12)!=index_reg||(extension&0x0F00u)!=0)return 0;
    displacement=(int8_t)(extension&0x00FFu);
    /* For brief PC-indexed EA, the extension word address is the PC base. */
    table=(int64_t)opcode+2+displacement;
    if(table<0||!in_range(size,(size_t)table,item_count*2u)||
       (table&1))return 0;
    for(size_t i=0;i<item_count;++i) {
        int16_t relative=(int16_t)read_be16(image+(size_t)table+i*2u);
        int64_t target=table+relative;
        if(target<0||target>=(int64_t)size||(target&1))return 0;
        targets[i]=(size_t)target;
    }
    *table_out=(size_t)table;
    return 1;
}

static int object_handler(const SegaCDViewSnapshot *s,size_t object_index,
                          unsigned object_id,uint32_t *address_out,
                          size_t *offset_out)
{
    const uint8_t *image=s->word_ram;
    uint32_t address;
    size_t index_offset;
    if(object_id==0u||object_id>0x12u||
       !in_range(s->word_ram_size,object_index+(object_id-1u)*4u,4u))
        return -1;
    index_offset=object_index+(object_id-1u)*4u;
    address=read_be32(image+index_offset);
    if(!address) { *address_out=0;*offset_out=0;return 0; }
    if(!source_pc(s,address,14u,offset_out))return -1;
    *address_out=address;
    return 1;
}

static void add_site(SegaCDCullDiscovery *out,SegaCDCullFamily family,
                     uint32_t function_pc,uint32_t base_pc,uint8_t object_id,
                     uint8_t object_x_offset)
{
    SegaCDCullSite *site=&out->sites[out->site_count++];
    site->family=family;
    site->function_pc=function_pc;
    site->subtract_pc=base_pc+12u;
    site->compare_pc=base_pc+22u;
    site->branch_pc=base_pc+26u;
    site->cleanup_pc=0;
    site->cleanup_kind=SEGACD_CULL_CLEANUP_NONE;
    site->object_id=object_id;
    site->object_x_offset=object_x_offset;
    site->object_register=0;
    site->camera_register=1;
}

static int branch_short_target(const uint8_t *image,size_t size,size_t opcode,
                               size_t *target_out)
{
    int8_t displacement;
    int64_t target;
    if(!in_range(size,opcode,2u)||image[opcode]!=0x62u||image[opcode+1u]==0)
        return 0;
    displacement=(int8_t)image[opcode+1u];
    target=(int64_t)opcode+2+displacement;
    if(target<0||target>=(int64_t)size||(target&1))return 0;
    *target_out=(size_t)target;
    return 1;
}

static int absolute_jump_target(const uint8_t *image,size_t size,size_t opcode,
                                size_t *target_out)
{
    uint32_t address;
    if(!in_range(size,opcode,6u)||read_be16(image+opcode)!=0x4EF9u)return 0;
    address=read_be32(image+opcode+2u);
    *target_out=(size_t)address;
    return 1;
}

static int mapped_address_offset(uint32_t image_base,size_t image_size,
                                 size_t address,size_t *offset_out)
{
    uint64_t delta;
    if(address>0xFFFFFFu||address<image_base)return 0;
    delta=(uint64_t)address-image_base;
    if(delta>=image_size)return 0;
    *offset_out=(size_t)delta;
    return 1;
}

static int is_delete_object(const uint8_t *image,size_t size,size_t offset,
                            size_t delete_object)
{
    return offset==delete_object&&in_range(size,offset,
           sizeof delete_object_anchor)&&
           !memcmp(image+offset,delete_object_anchor,
                   sizeof delete_object_anchor);
}

static int is_despawn_saved_flags(const uint8_t *image,size_t size,
                                  uint32_t image_base,size_t offset,
                                  size_t delete_object)
{
    size_t target_address,target;
    if(!in_range(size,offset,sizeof despawn_saved_flags_prefix+6u)||
       memcmp(image+offset,despawn_saved_flags_prefix,
              sizeof despawn_saved_flags_prefix)||
       !absolute_jump_target(image,size,offset+sizeof despawn_saved_flags_prefix,
                             &target_address)||
       !mapped_address_offset(image_base,size,target_address,&target))return 0;
    return is_delete_object(image,size,target,delete_object);
}

/* Returns 1 for a fully proven cleanup, 2 for the one explicitly recorded
 * R43C data-target case, and 0 for a malformed/unknown recognized window. */
static int classify_short_cleanup(const uint8_t *image,size_t size,size_t p,
                                  uint32_t image_base,uint8_t x_offset,
                                  size_t delete_object,
                                  SegaCDCullCleanupKind *kind_out,
                                  size_t *cleanup_out)
{
    size_t branch_target,tail=p+30u,target;
    if(!branch_short_target(image,size,p+26u,&branch_target)||
       !in_range(size,p,30u)||read_be16(image+p+28u)!=0x4E75u||
       branch_target!=tail||!in_range(size,tail,2u))return 0;

    if(x_offset==0x38u) {
        /* Palmtree Panic platform: LEA objPlayerSlot; JSR GetOffObject;
         * BRA.W ObjPlatform_Destroy. This proves the dismount helper and its
         * target's saved-flags/DeleteObject cleanup chain, without an ObjectIndex
         * identity assumption for any other zone. */
        size_t get_off_address,get_off_offset;
        if(!in_range(size,tail,14u)||read_be16(image+tail)!=0x43F8u||
           read_be16(image+tail+2u)!=0xD000u||
           read_be16(image+tail+4u)!=0x4EB9u||
           !read_be32(image+tail+6u))return 0;
        get_off_address=read_be32(image+tail+6u);
        if(!mapped_address_offset(image_base,size,get_off_address,&get_off_offset)||
           !in_range(size,get_off_offset,sizeof get_off_object_anchor)||
           memcmp(image+get_off_offset,get_off_object_anchor,
                  sizeof get_off_object_anchor)||
           read_be16(image+tail+10u)!=0x6000u||
           !branch_word_target(image,size,tail+10u,0x6000u,&target)||
           !is_despawn_saved_flags(image,size,image_base,target,delete_object))return 0;
        *kind_out=SEGACD_CULL_CLEANUP_PLATFORM_DESTROY;
        *cleanup_out=target;
        return 1;
    }

    if(x_offset!=0x08u&&x_offset!=0x32u&&x_offset!=0x36u)return 0;
    if(in_range(size,tail,sizeof despawn_saved_flags_prefix)&&
       !memcmp(image+tail,despawn_saved_flags_prefix,
               sizeof despawn_saved_flags_prefix)) {
        if(!is_despawn_saved_flags(image,size,image_base,tail,delete_object))return 0;
        *kind_out=SEGACD_CULL_CLEANUP_DESPAWN_OBJECT;
        *cleanup_out=tail;
        return 1;
    }

    /* Some overlays route through a BRA.W or directly enter the shared
     * saved-flags helper after the short branch. */
    if(read_be16(image+tail)==0x6000u) {
        if(!branch_word_target(image,size,tail,0x6000u,&target)||
           !is_despawn_saved_flags(image,size,image_base,target,delete_object))return 0;
        *kind_out=SEGACD_CULL_CLEANUP_DESPAWN_OBJECT;
        *cleanup_out=target;
        return 1;
    }
    if(x_offset==0x36u&&read_be16(image+tail)==0x4EF9u) {
        size_t target_address;
        if(!absolute_jump_target(image,size,tail,&target_address))return 0;
        if(mapped_address_offset(image_base,size,target_address,&target)&&
           is_delete_object(image,size,target,delete_object)) {
            *kind_out=SEGACD_CULL_CLEANUP_DELETE_OBJECT;
            *cleanup_out=target;
            return 1;
        }
        /* This exact R43C target is a source-disassembly data/extension table,
         * confirmed by its full mapped address and bytes. Preserve it as an
         * explicit unqualified record; all other mismatched JMP chains fail. */
        if(target_address==0x203B56u&&
           mapped_address_offset(image_base,size,target_address,&target)&&
           in_range(size,target,sizeof unqualified_r43c_table_prefix)&&
           !memcmp(image+target,unqualified_r43c_table_prefix,
                   sizeof unqualified_r43c_table_prefix))return 2;
    }
    return 0;
}

static int discover_short_cleanup_sites(const SegaCDViewSnapshot *s,
                                        size_t delete_object,
                                        SegaCDCullDiscovery *out)
{
    const uint8_t *image=s->word_ram;
    size_t short_count=0,qualified_count=0;
    for(size_t p=0;p+30u<=s->word_ram_size;p+=2u) {
        uint16_t x_offset;
        SegaCDCullCleanupKind kind=SEGACD_CULL_CLEANUP_NONE;
        size_t cleanup=0;
        int status;
        if(read_be16(image+p)!=0x3028u)continue;
        x_offset=read_be16(image+p+2u);
        if(x_offset!=0x08u&&x_offset!=0x32u&&x_offset!=0x36u&&
           x_offset!=0x38u)continue;
        if(read_be16(image+p+4u)!=0x0240u||
           read_be16(image+p+6u)!=0xFF80u||
           read_be16(image+p+8u)!=0x3238u||
           read_be16(image+p+10u)!=0xF700u||
           read_be16(image+p+12u)!=0x0441u||
           read_be16(image+p+14u)!=0x0080u||
           read_be16(image+p+16u)!=0x0241u||
           read_be16(image+p+18u)!=0xFF80u||
           read_be16(image+p+20u)!=0x9041u||
           read_be16(image+p+22u)!=0x0C40u||
           read_be16(image+p+24u)!=0x0280u)continue;
        /* 0x6200 is BHI.W with an extension word and belongs to the fixed
         * source culls above; only a nonzero low byte is this BHI.S family. */
        if(!in_range(s->word_ram_size,p,30u)||image[p+26u]!=0x62u||
           image[p+27u]==0)continue;
        ++short_count;
        status=classify_short_cleanup(image,s->word_ram_size,p,
                                      s->word_ram_base,(uint8_t)x_offset,
                                      delete_object,&kind,&cleanup);
        if(status==2) {
            if(out->unqualified_short_cleanup_count!=0u)return 0;
            out->unqualified_short_cleanup_count=1u;
            out->unqualified_short_cleanup_pc=s->word_ram_base+(uint32_t)p;
            continue;
        }
        if(status!=1||out->site_count>=SEGACD_CULL_MAX_SITES)return 0;
        ++qualified_count;
        {
            SegaCDCullSite *site=&out->sites[out->site_count++];
            memset(site,0,sizeof *site);
            site->family=SEGACD_CULL_SHORT_CLEANUP;
            site->function_pc=0; /* Unknown zonal identity; exact window is root. */
            site->subtract_pc=s->word_ram_base+(uint32_t)p+12u;
            site->compare_pc=s->word_ram_base+(uint32_t)p+22u;
            site->branch_pc=s->word_ram_base+(uint32_t)p+26u;
            site->cleanup_pc=s->word_ram_base+(uint32_t)cleanup;
            site->cleanup_kind=kind;
            site->object_id=0;
            site->object_x_offset=(uint8_t)x_offset;
            site->object_register=0;
            site->camera_register=1;
        }
    }
    /* Owned 70-file inventory: 123 recognized windows, one exact unqualified
     * R43C target, and no other short forms. */
    return short_count==qualified_count+out->unqualified_short_cleanup_count;
}

static int source_cull_branch(const uint8_t *image,size_t size,size_t base,
                              const uint8_t *prefix,size_t prefix_size,
                              size_t delete_object,size_t *compare_out)
{
    size_t branch_target;
    if(!in_range(size,base,prefix_size+2u)||
       memcmp(image+base,prefix,prefix_size)||prefix_size<2u||
       prefix[prefix_size-2u]!=0x62u||prefix[prefix_size-1u]!=0x00u||
       !branch_word_target(image,size,base+prefix_size-2u,0x6200u,
                           &branch_target)||branch_target!=delete_object)
        return 0;
    *compare_out=base+prefix_size-6u;
    return 1;
}

static int resolve_dispatch(const uint8_t *image,size_t size,size_t root,
                            unsigned state_count,size_t targets[16])
{
    size_t load_table,jump_table;
    size_t ignored[16];
    if(!in_range(size,root,sizeof dispatcher)||
       memcmp(image+root,dispatcher,sizeof dispatcher)||
       !indexed_word_table(image,size,root+6u,0x303Bu,0,state_count,
                           &load_table,ignored)||
       !indexed_word_table(image,size,root+10u,
                           (read_be16(image+root+10u)==0x4EFBu)?0x4EFBu:0x4EBBu,
                           0,state_count,&jump_table,targets)||
       load_table!=jump_table)return 0;
    return 1;
}

static int discover_object_site(const SegaCDViewSnapshot *s,
                                size_t object_index,unsigned object_id,
                                SegaCDCullFamily family,
                                const uint8_t *cull,size_t cull_size,
                                unsigned state_count,uint8_t x_offset,
                                SegaCDCullDiscovery *out,
                                size_t delete_object)
{
    const uint8_t *image=s->word_ram;
    uint32_t handler_address;
    size_t root,table,states[16],ignored[16],selected=0,compare;
    int status=object_handler(s,object_index,object_id,&handler_address,&root);
    if(status<0)return 0;
    if(status==0) {
        out->absent_families|=1u<<(unsigned)family;
        return 1;
    }

    if(family==SEGACD_CULL_TEST_BADNIK) {
        size_t hits=0;
        if(!resolve_dispatch(image,s->word_ram_size,root,state_count,states)) {
            out->absent_families|=1u<<(unsigned)family;
            return 1;
        }
        /* The source Test Badnik init begins with the offscreen-object test,
         * branches to DeleteObject, then advances its routine. This separates
         * ID $06's Test Badnik from the other generic two-state dispatchers
         * used by Round 4. Once that identity is present, a missing main cull
         * is a malformed/unsupported image rather than a silent absence. */
        if(!in_range(s->word_ram_size,states[0],14u)||
           memcmp(image+states[0],
                  (const uint8_t[]){0x08,0x28,0x00,0x07,0x00,0x22,
                                    0x66,0x00},8u)||
           !branch_word_target(image,s->word_ram_size,states[0]+6u,
                               0x6600u,&selected)||selected!=delete_object||
           memcmp(image+states[0]+10u,
                  (const uint8_t[]){0x54,0x28,0x00,0x24},4u)) {
            out->absent_families|=1u<<(unsigned)family;
            return 1;
        }
        for(size_t i=0;i<state_count;++i) {
            if(in_range(s->word_ram_size,states[i],cull_size)&&
               !memcmp(image+states[i],cull,cull_size)) {
                selected=states[i];++hits;
            }
        }
        if(hits!=1u||!source_cull_branch(image,s->word_ram_size,selected,
             cull,cull_size,delete_object,&compare))return 0;
        add_site(out,family,(uint32_t)(s->word_ram_base+root),
                 (uint32_t)(s->word_ram_base+selected),
                 (uint8_t)object_id,x_offset);
        return 1;
    }

    if(family==SEGACD_CULL_SPRING) {
        size_t hits=0;
        size_t table2;
        if(!in_range(s->word_ram_size,root,0x22u)||
           memcmp(image+root,(const uint8_t[]){0x0C,0x28,0x00,0x05,0x00,0x25,
                                               0x67,0xAA,0x70,0x00,0x10,0x28,
                                               0x00,0x24,0x67,0x06,0x4A,0x28,
                                               0x00,0x01,0x6A,0x08},0x16u)) {
            out->absent_families|=1u<<(unsigned)family;
            return 1;
        }
        if(!indexed_word_table(image,s->word_ram_size,root+0x16u,0x323Bu,0,
                               14u,&table,states)||
           !indexed_word_table(image,s->word_ram_size,root+0x1Au,0x4EBBu,1,
                               14u,&table2,ignored)||table!=table2)return 0;
        for(size_t q=root+0x20u;
            q<=root+0x100u&&in_range(s->word_ram_size,q,cull_size);q+=2u) {
            if(!memcmp(image+q,cull,cull_size)) { selected=q;++hits; }
        }
        if(hits!=1u||!source_cull_branch(image,s->word_ram_size,selected,
             cull,cull_size,delete_object,&compare))return 0;
        add_site(out,family,(uint32_t)(s->word_ram_base+root),
                 (uint32_t)(s->word_ram_base+selected),
                 (uint8_t)object_id,x_offset);
        return 1;
    }

    if(!resolve_dispatch(image,s->word_ram_size,root,state_count,states)) {
        out->absent_families|=1u<<(unsigned)family;
        return 1;
    }
    /* MovingSpring and FloatBlock test the wrapper immediately after the
     * indexed JSR returns. A different wrapper at this object ID is an
     * absent family, not a near-match candidate. */
    if(!in_range(s->word_ram_size,root+14u,cull_size)||
       memcmp(image+root+14u,cull,cull_size)) {
        out->absent_families|=1u<<(unsigned)family;
        return 1;
    }
    selected=root+14u;
    if(!source_cull_branch(image,s->word_ram_size,selected,cull,cull_size,
                           delete_object,&compare))return 0;
    add_site(out,family,(uint32_t)(s->word_ram_base+root),
             (uint32_t)(s->word_ram_base+selected),
             (uint8_t)object_id,x_offset);
    return 1;
}

static int discover_s1(const SegaCDViewSnapshot *s,size_t draw_object,
                       size_t delete_object,SegaCDCullDiscovery *out)
{
    const uint8_t *image=s->word_ram;
    size_t hits=0,selected=0,compare=0;
    for(size_t p=0;p+0x3Au<=s->word_ram_size;p+=2u) {
        size_t no_draw,draw_target,delete_target;
        if(memcmp(image+p,s1_cull_prefix,sizeof s1_cull_prefix))continue;
        /* BHI.W .NoDraw reaches the savedFlags cleanup block at +$22. */
        if(!branch_word_target(image,s->word_ram_size,p+0x1Au,0x6200u,
                               &no_draw)||no_draw!=p+0x22u||
           !branch_word_target(image,s->word_ram_size,p+0x1Eu,0x6000u,
                               &draw_target)||draw_target!=draw_object||
           memcmp(image+p+0x22u,s1_cleanup,sizeof s1_cleanup))continue;
        /* The cleanup's BEQ skips the BCLR, and the final BRA.W is the real
         * DeleteObject path. */
        if(image[p+0x2Eu]!=0x67u||image[p+0x2Fu]!=0x06u||
           !branch_word_target(image,s->word_ram_size,p+0x36u,0x6000u,
                               &delete_target)||delete_target!=delete_object)
            continue;
        selected=p;++hits;compare=p+0x16u;
    }
    if(hits!=1u)return 0;
    {
        SegaCDCullSite *site=&out->sites[out->site_count++];
        site->family=SEGACD_CULL_CHECK_S1;
        site->function_pc=(uint32_t)(s->word_ram_base+selected);
        site->subtract_pc=(uint32_t)(s->word_ram_base+selected+0x0Cu);
        site->compare_pc=(uint32_t)(s->word_ram_base+compare);
        site->branch_pc=(uint32_t)(s->word_ram_base+selected+0x1Au);
        site->cleanup_pc=0;
        site->cleanup_kind=SEGACD_CULL_CLEANUP_NONE;
        site->object_id=0;
        site->object_x_offset=0x08u;
        site->object_register=0;
        site->camera_register=1;
    }
    return 1;
}

int segacd_culls_discover(const SegaCDViewSnapshot *s,
                          SegaCDCullDiscovery *out)
{
    const uint8_t *image;
    size_t run,index_offset,draw_object,delete_object;
    SegaCDCullDiscovery temp;
    uint32_t table_address;
    if(!s||!out||!s->word_ram||s->word_ram_size!=0x40000u||
       (s->word_ram_base&1u)||s->word_ram_base>0xFFFFFFu||
       (uint64_t)s->word_ram_base+s->word_ram_size>0x1000000u)
        return 0;
    image=s->word_ram;
    memset(&temp,0,sizeof temp);

    if(!find_unique(image,s->word_ram_size,0,s->word_ram_size,
                    run_objects,sizeof run_objects,&run)||
       !in_range(s->word_ram_size,run+10u,sizeof run_objects_tail)||
       memcmp(image+run+10u,run_objects_tail,sizeof run_objects_tail))return 0;
    table_address=read_be32(image+run+6u);
    if(!source_pc(s,table_address,0x12u*4u,&index_offset))return 0;
    if(!find_unique(image,s->word_ram_size,0,s->word_ram_size,
                    draw_object_anchor,sizeof draw_object_anchor,&draw_object)||
       !find_unique(image,s->word_ram_size,0,s->word_ram_size,
                    delete_object_anchor,sizeof delete_object_anchor,
                    &delete_object))return 0;

    if(!discover_s1(s,draw_object,delete_object,&temp))return 0;
    if(!discover_object_site(s,index_offset,0x06u,SEGACD_CULL_TEST_BADNIK,
          test_badnik_cull,sizeof test_badnik_cull,2u,0x30u,&temp,
          delete_object)||
       !discover_object_site(s,index_offset,0x12u,SEGACD_CULL_FLOAT_BLOCK,
          float_block_cull,sizeof float_block_cull,7u,0x08u,&temp,
          delete_object)||
       !discover_object_site(s,index_offset,0x0Fu,SEGACD_CULL_MOVING_SPRING,
          moving_spring_cull,sizeof moving_spring_cull,3u,0x36u,&temp,
          delete_object)||
       !discover_object_site(s,index_offset,0x0Au,SEGACD_CULL_SPRING,
          moving_spring_cull,sizeof moving_spring_cull,14u,0x36u,&temp,
          delete_object)||
       !discover_short_cleanup_sites(s,delete_object,&temp))return 0;

    if(temp.site_count<1u||temp.site_count>SEGACD_CULL_MAX_SITES)return 0;
    *out=temp;
    return 1;
}

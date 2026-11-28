#include "m68k_floor.h"
#include "m68k_decoder.h"
#include "m68k_validator.h"
#include "m68k_timing.h"
#include "m68k_step_bind.h"
#include <limits.h>
#include <string.h>

#include "m68k_floor_ops.inc"

typedef struct {M68kStepContext *cpu;uint32_t address;uint16_t word;int have;} Fetch;
static uint8_t fetch_byte(uint32_t address,void *user)
{
    Fetch *f=user;address&=0xFFFFFFu;
    if(!f->have || f->address!=(address&~1u)) {
        f->address=address&~1u;f->word=m68k_step_fetch16(f->cpu,f->address);f->have=1;
    }
    return (uint8_t)((address&1u)?f->word:f->word>>8);
}
static void divide(M68kStepContext *ctx,const M68KInstr *ins,ExtR *er)
{
    uint16_t source=(uint16_t)read_ea(ctx,ins,ins->src_ea,M68K_SIZE_W,er);
    unsigned extra=m68k_68000_operand_cycles((unsigned)ins->src_ea,0);
    if(!source){m68k_step_exception(ctx,5,g_cpu.PC,38u+extra);return;}
    ctx->charge=(ins->mnemonic==MN_DIVU?140u:158u)+extra;
    uint32_t dest=g_cpu.D[ins->reg],quotient,remainder;int overflow;
    if(ins->mnemonic==MN_DIVS) {
        /* Widen before division: INT32_MIN / -1 is representable here. */
        int64_t dividend=(int32_t)dest,divisor=(int16_t)source;
        int64_t q=dividend/divisor,r=dividend%divisor;
        overflow=q<INT16_MIN || q>INT16_MAX;
        quotient=(uint32_t)q;remainder=(uint32_t)r;
    } else {
        quotient=dest/source;remainder=dest%source;overflow=quotient>UINT16_MAX;
    }
    g_cpu.SR&=0xFFF0u;
    if(overflow){g_cpu.SR|=10u;return;}
    g_cpu.D[ins->reg]=(quotient&65535u)|(remainder<<16);
    if(quotient&32768u)g_cpu.SR|=8;
    if(!(quotient&65535u))g_cpu.SR|=4;
}
static int control(M68kStepContext *ctx,const M68KInstr *ins)
{
    ExtR er;er_init(&er,ins);uint32_t next=g_cpu.PC;
    unsigned condition=(ins->words[0]>>8)&15u;
    switch(ins->mnemonic) {
    case MN_RTS:g_cpu.PC=m68k_read32(g_cpu.A[7])&0xFFFFFFu;g_cpu.A[7]+=4;ctx->charge=16;return 1;
    case MN_RTE:ctx->charge=20;m68k_step_rte(ctx);return 1;
    case MN_RTR: {
        uint32_t sp=g_cpu.A[7];uint16_t ccr=m68k_read16(sp);uint32_t pc=m68k_read32(sp+2);
        g_cpu.A[7]=sp+6;g_cpu.SR=(uint16_t)((g_cpu.SR&0xFF00u)|(ccr&31u));g_cpu.PC=pc&0xFFFFFFu;
        ctx->charge=20;return 1;
    }
    case MN_STOP:ctx->charge=4;m68k_step_stop(ctx,(uint16_t)ins->imm32);return 1;
    case MN_RESET:ctx->charge=132;m68k_step_reset_devices(ctx);return 1;
    case MN_TRAP:m68k_step_exception(ctx,32u+(ins->imm32&15u),next,34);return 1;
    case MN_TRAPV:ctx->charge=4;if(g_cpu.SR&2u)m68k_step_exception(ctx,7,next,34);return 1;
    case MN_ILLEGAL:case MN_MOVEC:m68k_step_illegal(ctx);return 1;
    case MN_BRA:case MN_BSR:case MN_Bcc:case MN_DBcc:
        if(!ins->has_target)return 0;
        if(ins->mnemonic==MN_BRA){ctx->charge=10;g_cpu.PC=ins->target_addr&0xFFFFFFu;}
        else if(ins->mnemonic==MN_BSR){ctx->charge=18;push32(ctx,next);g_cpu.PC=ins->target_addr&0xFFFFFFu;}
        else if(ins->mnemonic==MN_Bcc) {
            if(eval_cond(ctx,(int)condition)){ctx->charge=10;g_cpu.PC=ins->target_addr&0xFFFFFFu;}
            else ctx->charge=ins->word_count==1?8u:12u;
        } else if(eval_cond(ctx,(int)condition))ctx->charge=12;
        else {
            uint16_t low=(uint16_t)(g_cpu.D[ins->reg]-1u);
            g_cpu.D[ins->reg]=(g_cpu.D[ins->reg]&0xFFFF0000u)|low;
            if(low!=65535u){ctx->charge=10;g_cpu.PC=ins->target_addr&0xFFFFFFu;}else ctx->charge=14;
        }
        return 1;
    case MN_JMP:case MN_JSR: {
        unsigned cycles=m68k_68000_jump_cycles((unsigned)ins->src_ea,ins->mnemonic==MN_JSR);
        if(!cycles)return 0;
        uint32_t target=addr_ea(ctx,ins,ins->src_ea,&er)&0xFFFFFFu;
        if(ins->mnemonic==MN_JSR)push32(ctx,next);
        g_cpu.PC=target;ctx->charge=cycles;return 1;
    }
    case MN_MOVE_USP:
        if(!m68k_step_supervisor(ctx))return 1;
        ctx->charge=4;
        if(ins->words[0]&8u)g_cpu.A[ins->reg]=g_cpu.USP;else g_cpu.USP=g_cpu.A[ins->reg];return 1;
    case MN_MOVE_SR:
        if(ins->dst_is_ea) {
            ctx->charge=(ins->src_ea>>3)==0?6u:8u+m68k_68000_operand_cycles((unsigned)ins->src_ea,0);
            (void)write_ea(ctx,ins,ins->src_ea,M68K_SIZE_W,&er,g_cpu.SR);
        } else {
            if(!m68k_step_supervisor(ctx))return 1;
            ctx->charge=12u+m68k_68000_operand_cycles((unsigned)ins->src_ea,0);
            uint16_t sr=(uint16_t)read_ea(ctx,ins,ins->src_ea,M68K_SIZE_W,&er);m68k_step_set_sr(ctx,sr);
        }
        return 1;
    case MN_MOVE_CCR:
        if(ins->dst_is_ea)m68k_step_illegal(ctx); /* 68010-only source form. */
        else {
            ctx->charge=12u+m68k_68000_operand_cycles((unsigned)ins->src_ea,0);
            uint16_t ccr=(uint16_t)read_ea(ctx,ins,ins->src_ea,M68K_SIZE_W,&er);
            g_cpu.SR=(uint16_t)((g_cpu.SR&0xFF00u)|(ccr&31u));
        }
        return 1;
    case MN_ORI_TO_SR:case MN_ANDI_TO_SR:case MN_EORI_TO_SR: {
        if(!m68k_step_supervisor(ctx))return 1;
        uint16_t sr=g_cpu.SR,value=(uint16_t)ins->imm32;
        if(ins->mnemonic==MN_ORI_TO_SR)sr|=value;else if(ins->mnemonic==MN_ANDI_TO_SR)sr&=value;else sr^=value;
        ctx->charge=20;m68k_step_set_sr(ctx,sr);return 1;
    }
    case MN_ORI_TO_CCR:case MN_ANDI_TO_CCR:case MN_EORI_TO_CCR: {
        uint16_t ccr=g_cpu.SR&31u,value=(uint16_t)ins->imm32;
        if(ins->mnemonic==MN_ORI_TO_CCR)ccr|=value;else if(ins->mnemonic==MN_ANDI_TO_CCR)ccr&=value;else ccr^=value;
        ctx->charge=20;g_cpu.SR=(uint16_t)((g_cpu.SR&0xFF00u)|(ccr&31u));return 1;
    }
    case MN_MOVEP: {
        /* Peripheral ports occupy alternating byte lanes. These are byte
         * transactions even when the effective address is odd; a word read
         * here would both access the wrong ports and raise a false fault. */
        uint32_t address=g_cpu.A[ins->src_ea&7]+(uint32_t)(int32_t)(int16_t)ins->words[1];
        unsigned count=(ins->words[0]&0x40u)?4u:2u;
        ctx->charge=count==4?24u:16u;
        if(ins->words[0]&0x80u) {
            uint32_t value=g_cpu.D[ins->reg];
            for(unsigned n=0;n<count;++n)m68k_write8(address+n*2u,(uint8_t)(value>>((count-1u-n)*8u)));
        } else {
            uint32_t value=0;
            for(unsigned n=0;n<count;++n)value=(value<<8)|m68k_read8(address+n*2u);
            if(count==2)value|=g_cpu.D[ins->reg]&0xFFFF0000u;
            g_cpu.D[ins->reg]=value;
        }
        return 1; /* MOVEP preserves all condition codes and address registers. */
    }
    case MN_DIVU:case MN_DIVS:divide(ctx,ins,&er);return 1;
    case MN_CHK: {
        unsigned extra=m68k_68000_operand_cycles((unsigned)ins->src_ea,0);
        int16_t bound=(int16_t)read_ea(ctx,ins,ins->src_ea,M68K_SIZE_W,&er),value=(int16_t)g_cpu.D[ins->reg];
        ctx->charge=10u+extra;
        if(value<0){g_cpu.SR|=8;m68k_step_exception(ctx,6,next,40u+extra);}
        else if(value>bound){g_cpu.SR&=0xFFF7u;m68k_step_exception(ctx,6,next,40u+extra);}
        return 1;
    }
    default:return 0;
    }
}
static void execute(M68kStepContext *ctx)
{
    Fetch fetch={ctx,ctx->state.instruction_pc,ctx->state.fault_opcode,1};
    GenesisRom view={0};view.rom_size=UINT32_MAX;view.read8_override=fetch_byte;view.read8_user=&fetch;
    M68KInstr ins;
    if(!m68k_decode_68000(&view,ctx->state.instruction_pc,&ins)){m68k_step_unimplemented(ctx);return;}
    if(m68k_validate(&ins,NULL)!=M68K_LEGAL || ins.mnemonic==MN_ILLEGAL) {m68k_step_illegal(ctx);return;}
    uint32_t next=(ins.addr+ins.byte_length)&0xFFFFFFu;g_cpu.PC=next;
    ctx->charge=m68k_68000_nominal_cycles(&ins);
    if(control(ctx,&ins))return;
    switch(ins.mnemonic) {
    case MN_LSL:case MN_LSR:case MN_ASL:case MN_ASR:case MN_ROL:case MN_ROR:case MN_ROXL:case MN_ROXR:
        if(!ins.mem_shift && (ins.words[0]&32u))ctx->charge=(ins.size==M68K_SIZE_L?8u:6u)+2u*(g_cpu.D[ins.src_ea]&63u);
        break;
    case MN_Scc:
        if((ins.src_ea>>3)==0)ctx->charge=eval_cond(ctx,(ins.words[0]>>8)&15)?6u:4u;
        break;
    default:break;
    }
    if(!floor_semantics(ctx,&ins,&next)) {g_cpu.PC=ctx->state.instruction_pc;m68k_step_unimplemented(ctx);return;}
    g_cpu.PC=next&0xFFFFFFu;
}
M68kStepResult segacd_m68k_floor_step(M68kStepContext *ctx)
{return m68k_step_execute_live(ctx,execute);}

#ifndef GENESIS_NATIVE_INTERRUPT_FRAME_H
#define GENESIS_NATIVE_INTERRUPT_FRAME_H
#include <stdint.h>

/* MC68000 format-0 interrupt frame: SR, then the interrupted PC. Guest
 * handlers may rewrite either field before RTE. Work RAM wraps at 64 KiB. */
static inline void native_interrupt_frame_write(uint8_t ram[65536],uint32_t sp,
                                                uint16_t sr,uint32_t pc)
{
    uint8_t bytes[6]={(uint8_t)(sr>>8),(uint8_t)sr,(uint8_t)(pc>>24),
        (uint8_t)(pc>>16),(uint8_t)(pc>>8),(uint8_t)pc};
    for(unsigned i=0;i<6;++i)ram[(sp+i)&65535u]=bytes[i];
}
static inline void native_interrupt_frame_read(const uint8_t ram[65536],uint32_t sp,
                                               uint16_t *sr,uint32_t *pc)
{
    *sr=(uint16_t)(((unsigned)ram[sp&65535u]<<8)|ram[(sp+1)&65535u]);
    *pc=0;
    for(unsigned i=2;i<6;++i)*pc=(*pc<<8)|ram[(sp+i)&65535u];
}
/* A completed native IRQ can satisfy the immediately following polling wait.
 * A newly requested routine still waits; explicit state capture may park. */
static inline int native_interrupt_wait_satisfied(int *completed,unsigned routine,int save)
{
    int ready=*completed;*completed=0;
    return ready && !routine && !save;
}
#endif

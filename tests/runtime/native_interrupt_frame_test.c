#include "../../runner/native_interrupt_frame.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    uint8_t ram[65536];memset(ram,0xA5,sizeof ram);
    uint32_t frame=0xFFFDFAu,pc;uint16_t sr;
    native_interrupt_frame_write(ram,frame,0x2315,0x1EC4);
    const uint8_t bytes[]={0x23,0x15,0,0,0x1E,0xC4};
    assert(!memcmp(ram+0xFDFA,bytes,6));
    /* VInt's15-register MOVEM and Set_Kos_Bookmark's JSR place the
     * interrupted PC at42(sp), exactly as in both original source profiles. */
    uint32_t helper_sp=frame-15*4-4,slot=helper_sp+0x42;
    assert(slot==frame+2);
    ram[slot&65535]=0;ram[(slot+1)&65535]=0;
    ram[(slot+2)&65535]=0x1F;ram[(slot+3)&65535]=0x96;
    native_interrupt_frame_read(ram,frame,&sr,&pc);
    assert(sr==0x2315 && pc==0x1F96 && ram[0xFDF9]==0xA5);
    native_interrupt_frame_write(ram,0xFFFFFEu,0x2700,0x123456);
    native_interrupt_frame_read(ram,0xFFFFFEu,&sr,&pc);
    assert(sr==0x2700 && pc==0x123456);
    int completed=1;
    assert(native_interrupt_wait_satisfied(&completed,0,0) && !completed);
    assert(!native_interrupt_wait_satisfied(&completed,0,0));
    completed=1;assert(!native_interrupt_wait_satisfied(&completed,8,0) && !completed);
    completed=1;assert(!native_interrupt_wait_satisfied(&completed,0,1) && !completed);
    return 0;
}

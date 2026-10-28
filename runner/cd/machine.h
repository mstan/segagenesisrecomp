/* Reusable NTSC Sega CD native machine. Original programs must supply guarded
 * Main/Sub instruction catalogs and an instruction-sized generated Z80 step.
 * This consumer never attaches the diagnostic execution floor. */
#ifndef SEGACD_NATIVE_MACHINE_H
#define SEGACD_NATIVE_MACHINE_H
#include "z80_native.h"
#include "../video/genesis_bus.h"

typedef struct {
    const M68kStepModule *const *main_modules,*const *sub_modules;
    size_t main_count,sub_count;
    void (*z80_step)(void);
} SegaCDNativeProgram;

typedef struct {
    SegaCD devices;
    SegaCDCoordinator coordinator;
    SegaCDZ80 z80;
    GenesisBus bus;
    GVDP vdp;
    uint64_t line,frames,bus_clock;
    uint32_t pixels[GVDP_MAX_HEIGHT][GVDP_MAX_WIDTH];
    unsigned width,height,irq4,irq6;
    M68kStepResult result;
    /* Emulation-thread host observers. They may queue output, never execute
     * CPUs. Audio events use absolute Sub-clock timestamps for both CPUs. */
    void (*audio_write)(void *,uint64_t,uint8_t port,uint8_t value);
    void *audio_user;
} SegaCDMachine;

/* Shared Genesis/Z80 ABIs currently allow one active machine per process.
 * No file/media ownership is transferred. BIOS is copied, catalogs must live
 * until close. Caller mounts verified media and loads persisted backup RAM. */
int segacd_machine_init(SegaCDMachine *,const void *bios,size_t bytes,
                        const SegaCDNativeProgram *);
void segacd_machine_close(SegaCDMachine *);
void segacd_machine_pad(SegaCDMachine *,unsigned port,uint16_t buttons);
M68kStepResult segacd_machine_step(SegaCDMachine *);
M68kStepResult segacd_machine_run_frame(SegaCDMachine *);
uint64_t segacd_machine_line_clock(uint64_t line);
/* Optional game presentation reads each completed active native scanline,
 * including live raster palette/scroll changes. The observer owns its output;
 * it must not mutate hardware or advance a CPU. Install between completed
 * operations. NULL detaches it. One active machine is supported by this ABI. */
typedef void (*SegaCDMachineVideoObserver)(void *,const SegaCDMachine *,unsigned line);
int segacd_machine_video_observer(SegaCDMachine *,SegaCDMachineVideoObserver,void *user);
/* Optional original-image production. Default observers see eager pixels.
 * An explicit deferred observer reads pixels only through the getter, which
 * resolves retained raster inputs with their original per-row palette and
 * never advances devices. Observer replacement returns to eager behavior.
 * Enable/disable between operations; allocation failure keeps the eager path.
 * The pixels field is a derived cache while deferral is enabled. */
int segacd_machine_defer_video(SegaCDMachine *,int enabled);
const uint32_t *segacd_machine_pixels(const SegaCDMachine *);
typedef struct {
    uint64_t captured_rows,composed_rows,eager_rows;
    size_t retained_bytes;
    int deferred;
} SegaCDMachineVideoStats;
int segacd_machine_video_stats(const SegaCDMachine *,SegaCDMachineVideoStats *);
#endif

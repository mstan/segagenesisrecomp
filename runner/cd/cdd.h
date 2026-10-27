/* Sega CD drive/gate-array protocol, independent of media handles and CPUs.
 * Hardware Manual pp31-33; CDD_inf v2.0 RE notes (see backend documentation).
 * Mechanical/packet timing is provisional until original-drive qualification. */
#ifndef SEGACD_CDD_H
#define SEGACD_CDD_H
#include <stdint.h>
typedef struct {int32_t index00,start,end;uint8_t data;} SegaCDTrack;
typedef struct {SegaCDTrack track[99];unsigned count;int32_t leadout;} SegaCDTOC;
typedef struct {
    SegaCDTOC toc;
    uint8_t tx[10],rx[10],pending[10];
    uint8_t control,status,report,query_track,after_seek,report_valid;
    uint8_t tray_open,command_pending,unsupported_command,motor_started;
    uint8_t track_skip_active,track_skip_inward;
    uint16_t fader;
    int32_t lba,seek_target,track_skip_origin_lba,track_skip_target_lba;
    int8_t scan;
    uint32_t track_skip_count,track_skip_step;
    uint64_t cycles,frame_origin,frames,command_deadline,seek_deadline,fader_deadline;
    uint64_t track_skip_origin_cycle,track_skip_deadline;
} SegaCDDrive;
/* Called once per delivered raw sector, for both audio and data. */
typedef void (*SegaCDSectorSink)(void *,int32_t lba);
void segacd_cdd_init(SegaCDDrive *);
void segacd_cdd_reset(SegaCDDrive *);
int segacd_cdd_mount(SegaCDDrive *,const SegaCDTOC *);
void segacd_cdd_unmount(SegaCDDrive *);
uint8_t segacd_cdd_read(const SegaCDDrive *,unsigned register_offset);
void segacd_cdd_write(SegaCDDrive *,unsigned register_offset,uint8_t);
/* Advances input clocks (12.5 MHz). Returns nonzero for a new status IRQ4. */
unsigned segacd_cdd_advance(SegaCDDrive *,uint32_t clocks,SegaCDSectorSink,void *);
uint64_t segacd_cdd_next_event(const SegaCDDrive *);
#endif

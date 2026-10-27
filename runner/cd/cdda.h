/* Portable CDDA source stream. First sector establishes sample phase; later
 * samples use exact 12.5 MHz / 44.1 kHz rational timing and a two-sector FIFO.
 * Startup phase is a functional profile, not measured DAC phase. Emphasis and
 * double speed currently fail explicitly. No host resampler or speaker I/O. */
#ifndef SEGACD_CDDA_H
#define SEGACD_CDDA_H
#include <stdint.h>
#include "fader.h"

enum { SEGACD_CDDA_CLOCK=12500000u, SEGACD_CDDA_RATE=44100u, SEGACD_CDDA_SECTOR_FRAMES=588u, SEGACD_CDDA_FIFO_SECTORS=2u };
typedef enum { SEGACD_CDDA_STOPPED=0, SEGACD_CDDA_PLAYING=1, SEGACD_CDDA_PAUSED=2 } SegaCDCddaTransport;
typedef enum { SEGACD_CDDA_OK=0, SEGACD_CDDA_INVALID, SEGACD_CDDA_NOT_PLAYING, SEGACD_CDDA_OVERFLOW, SEGACD_CDDA_UNDERFLOW, SEGACD_CDDA_UNSUPPORTED } SegaCDCddaResult;
enum {
    SEGACD_CDDA_STATUS_OVERFLOW=1u<<0, SEGACD_CDDA_STATUS_UNDERFLOW=1u<<1,
    SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED=1u<<2,
    SEGACD_CDDA_STATUS_DOUBLE_SPEED_UNSUPPORTED=1u<<3
};
typedef void (*SegaCDCddaSample)(void *,uint64_t subclock,int16_t left,int16_t right);
typedef struct {
    /* Explicit deterministic state. No host pointers or wall-clock fields. */
    int16_t fifo[SEGACD_CDDA_FIFO_SECTORS][SEGACD_CDDA_SECTOR_FRAMES][2];
    int32_t fifo_lba[SEGACD_CDDA_FIFO_SECTORS];
    uint64_t clock_now,epoch_origin,phase,total_sample_ticks,delivered_frames,underflow_frames,silence_frames,sectors_accepted;
    uint16_t fader_raw_target,status;
    uint8_t fifo_head,fifo_count,transport,source_anchored,deemphasis,double_speed;
    uint16_t fifo_frame;
    SegaCDFader fader;
} SegaCDCdda;

void segacd_cdda_reset(SegaCDCdda *,uint64_t reset_clock,uint16_t initial_fader_raw);
void segacd_cdda_play(SegaCDCdda *);
void segacd_cdda_pause(SegaCDCdda *);
void segacd_cdda_stop(SegaCDCdda *);
void segacd_cdda_seek(SegaCDCdda *);
void segacd_cdda_clear_for_data(SegaCDCdda *);
SegaCDCddaResult segacd_cdda_set_modes(SegaCDCdda *,int deemphasis,int double_speed);
/* Snapshots FD immediately. DEF1/DEF0 or SSF requests are rejected and set
 * sticky unsupported status. EFDT remains a CDD status/command, not a stream API. */
SegaCDCddaResult segacd_cdda_set_fader_target_raw(SegaCDCdda *,uint16_t raw);
/* Queue one raw 2352-byte audio sector at its absolute 12.5 MHz subclock.
 * Call after advancing inclusively through the event boundary: stamp must
 * equal clock_now. This profile consumes any sample due at that instant from
 * already queued audio, then exposes the newly delivered sector. */
SegaCDCddaResult segacd_cdda_push_raw_sector(SegaCDCdda *,uint64_t stamp,int32_t lba,const uint8_t raw[2352]);
/* Output due DAC frames through absolute stamp. Missing playing audio sets a
 * sticky underflow and emits no fabricated sample; inactive transport emits
 * clocked zero frames. Sink is per stereo frame; fader ticks exactly once. */
SegaCDCddaResult segacd_cdda_advance_to(SegaCDCdda *,uint64_t stamp,SegaCDCddaSample,void *,uint64_t *frames_emitted);

#endif

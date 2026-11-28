#ifndef SEGACD_QA_DEBUG_H
#define SEGACD_QA_DEBUG_H

#include "machine.h"

typedef struct SegaCDDebugServer SegaCDDebugServer;
typedef void (*SegaCDDebugWriteLog)(void *user,const char *space,
                                    uint32_t offset,const uint8_t *bytes,size_t size);
typedef int (*SegaCDDebugCapture)(void *user,const char *path,const SegaCDMachine *machine);

/* Bind only to IPv4 loopback. Poll from the emulation thread between frames;
 * this is the sole place requests inspect or modify machine state. Commands
 * are newline-delimited: status; read SPACE OFFSET COUNT; write SPACE OFFSET
 * BYTE; pad PORT MASK; pause 0|1; step [FRAMES]; quit; capture PATH;
 * inspect. Numbers accept C base prefixes. read returns hex bytes; write
 * handles one byte. inspect copies cached machine state without bus accesses. */
SegaCDDebugServer *segacd_debug_start(SegaCDMachine *,uint16_t port,
                                      SegaCDDebugWriteLog,SegaCDDebugCapture,
                                      void *user,char *error,size_t error_size);
void segacd_debug_stop(SegaCDDebugServer *);
uint16_t segacd_debug_port(const SegaCDDebugServer *);
void segacd_debug_poll(SegaCDDebugServer *);
/* A paused runner should call poll and skip frame execution unless this says
 * to run. Each queued step credit grants one frame. */
int segacd_debug_should_run_frame(SegaCDDebugServer *);
int segacd_debug_quit_requested(const SegaCDDebugServer *);
/* Pad masks remain held until that port receives an explicit zero mask,
 * including across client disconnects. Combine these with local input. */
uint16_t segacd_debug_pad(const SegaCDDebugServer *,unsigned port);
/* Shared pause state for commands and host controls such as the P key. */
void segacd_debug_pause(SegaCDDebugServer *,int paused);
int segacd_debug_is_paused(const SegaCDDebugServer *);

#endif

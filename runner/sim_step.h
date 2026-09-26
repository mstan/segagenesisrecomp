#ifndef GENESIS_SIM_STEP_H
#define GENESIS_SIM_STEP_H
/*
 * sim_step.h -- ONE simulation tick (one NTSC wall frame) from sealed inputs.
 *
 * Every consumer of a tick -- the offline frame loop, the netplay live admit,
 * a rollback replay and the determinism probe -- runs the SAME function, so
 * "what a tick is" has one definition (recomp-ai-rules/NETPLAY.md section 2:
 * only published inputs may enter the simulation).
 *
 * A tick is: arm the frame (glue_reset_frame_sync / glue_run_game_frame),
 * apply the sealed pads to the two physical ports, run the host pre-raster
 * hook (widescreen pin, trace stamps), apply the scheduler's frame-boundary
 * rule (glue_sched_frame_begin), run 262 scanlines (machine_run_frame: the
 * game fiber, the Z80 and the VDP interleaved), drain the audio chips for the
 * whole wall frame into the caller's buffers, run the host post-drain hook,
 * then the V-blank bookkeeping (glue_service_vblank, glue_end_of_wall_frame).
 *
 * The audio drain is SIMULATION: it advances the YM2612/PSG models and
 * consumes the cycle-stamped event queue. It must run on every tick,
 * including a replayed one; a replay passes scratch buffers so what it
 * produces is never played twice (the device stream is never rewound).
 *
 * The scanline SINK is presentation: NULL (a replayed tick) skips the ARGB
 * conversion and the sink. Rendering itself still runs, because sprite
 * evaluation sets the VDP status flags the 68K reads (genesis_machine.c).
 */
#include <stddef.h>
#include <stdint.h>

#include "video/genesis_machine.h"   /* GenesisScanlineSink */

#ifdef __cplusplus
extern "C" {
#endif

/* Logical players a tick carries. Ports 0/1 are the Genesis bus; players
 * 2/3 exist only for game adapters (Sonic 2's party) via genesis_sim_pad. */
#define GENESIS_SIM_MAX_PLAYERS 4

typedef struct GenesisSimInput {
    /* GPAD_* bits (genesis_bus.h), 1 = pressed: active high, 12 bits
     * (8 standard + X/Y/Z/Mode). Neutral = 0. */
    uint16_t pad[GENESIS_SIM_MAX_PLAYERS];
    /* Controller type on the two physical ports: 0 = 3-button, 1 = 6-button.
     * Session-pinned online. */
    uint8_t  pad_type[2];
    /* Bit p: player p is driven by a human this tick (a connected device or
     * script offline; an occupied seat online). Adapters decide human vs
     * CPU companions from this, never from a live device query. */
    uint32_t human_mask;
} GenesisSimInput;

/* Where the tick's audio goes. The drain writes (never accumulates) up to
 * *_cap frames; *_count receives the frames written. */
typedef struct GenesisSimAudio {
    int16_t *fm;   size_t fm_cap;   size_t *fm_count;    /* stereo frames */
    int16_t *psg;  size_t psg_cap;  size_t *psg_count;   /* mono frames   */
} GenesisSimAudio;

typedef struct GenesisSimHooks {
    /* Before the raster, after the pads are applied. Host work that sets
     * session-pinned simulation parameters (widescreen margin) and trace
     * stamps. Optional. */
    void (*pre_raster)(void *ctx);
    /* Scanline sink; NULL = render for status only, present nothing. */
    GenesisScanlineSink sink;
    void *sink_user;
    /* After the audio drain, before V-blank bookkeeping (pre-resample
     * capture, observability, reverse-debug park). Return 0 to stop the
     * tick there (the host is quitting). Optional. */
    int (*post_drain)(void *ctx);
    void *ctx;
} GenesisSimHooks;

/* Run one tick. Returns 1 when the tick completed, 0 when post_drain asked
 * to stop. Must be called from the scheduler (main) fiber. */
int genesis_sim_step(const GenesisSimInput *in, const GenesisSimAudio *audio,
                     const GenesisSimHooks *hooks);

/* The sealed inputs of the tick in progress (or last run). Game adapters read
 * players through these instead of SDL / input maps. */
uint16_t genesis_sim_pad(int player);
uint32_t genesis_sim_human_mask(void);
const GenesisSimInput *genesis_sim_input(void);

/* Rollback: the sealed input is part of the tick's state (restored with a
 * snapshot so a replayed tick's first reads see the same seal). */
void genesis_sim_input_restore(const GenesisSimInput *in);

/* Ticks completed since process start / the last genesis_sim_reset(). */
uint32_t genesis_sim_tick_count(void);
void     genesis_sim_set_tick_count(uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* GENESIS_SIM_STEP_H */

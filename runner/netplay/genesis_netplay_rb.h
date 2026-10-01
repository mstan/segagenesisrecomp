#ifndef GENESIS_NETPLAY_RB_H
#define GENESIS_NETPLAY_RB_H
/*
 * genesis_netplay_rb.h -- the Genesis half of rollback: recomp-net's episode
 * driver (recomp_net/rb_driver.h) bound to the engine through RNetRbHost.
 *
 * The engine supplies what is genuinely its own: snapshots (runner/rb_state.c
 * in rbengine's tick-keyed ring), one tick (genesis_sim_step through the
 * bound runner), the partitioned digest, the pad layout (4-byte blob, GPAD_*
 * active high, neutral 0) and presentation suppression during a replay.
 * Everything that decides WHEN to rewind is the driver's.
 *
 * Replay shape: INLINE (see genesis_netplay_rb.c for the measured
 * justification).
 */
#include <stdint.h>

#include "recomp_net/recomp_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GenesisNetplayRbBindings {
    RNetSession **session;
    int *local_slot;
    int *slot_count;
    int *input_delay;
    int *input_prediction;
    uint32_t occupied_mask;
    /* rows by session slot (GPAD_* bits) for the tick about to run */
    void (*publish)(uint32_t tick, const uint16_t *rows, int slots);
    /* run one tick on the rows last published (replay) */
    void (*run_tick)(void);
} GenesisNetplayRbBindings;

void genesis_netplay_rb_bind(const GenesisNetplayRbBindings *b);
int  genesis_netplay_rb_start(void);
void genesis_netplay_rb_shutdown(void);
int  genesis_netplay_rb_poll_admit(void);
void genesis_netplay_rb_finish_frame(void);
/* 1 between resim_begin and resim_end (a replay is running). */
int  genesis_netplay_rb_in_resim(void);

uint32_t genesis_netplay_rb_sim_tick(void);
const char *genesis_netplay_rb_refusal(void);
int  genesis_netplay_rb_quiesced(void);
int  genesis_netplay_rb_draining(void);
void genesis_netplay_rb_request_quiesce(void);
int  genesis_netplay_rb_last_fork(uint32_t *tick, const char **partition,
                                  uint32_t *mine, uint32_t *theirs);

/* Process properties (survive matches): identity and the sealed config
 * image carried in the mod-set handshake. */
void genesis_netplay_rb_set_identity(uint32_t build_fp, uint32_t content_fp);
void genesis_netplay_rb_set_config_image(const char *text);

/* The host loop reports each live tick's cost (NETPLAY_FIELDS). */
void genesis_netplay_rb_note_live_tick_us(uint32_t us);

/* One NETPLAY_DRIVER summary line (harness ledger reads it). */
void genesis_netplay_rb_print_summary(void);

#ifdef __cplusplus
}
#endif

#endif

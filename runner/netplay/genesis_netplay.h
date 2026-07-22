#ifndef GENESIS_NETPLAY_H
#define GENESIS_NETPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GenesisNetplayConfig {
    int      enabled;
    int      local_slot;
    int      input_player;
    int      input_delay;
    uint32_t session_id;
    char     bind_hostport[64];
    char     peer_hostport[64];
    /* 0 = automatic, 1 = force ICE, 2 = force LAN. */
    int      transport;
} GenesisNetplayConfig;

void genesis_netplay_config_defaults(GenesisNetplayConfig *cfg);
void genesis_netplay_apply_env(GenesisNetplayConfig *cfg);

int      genesis_netplay_active(void);
int      genesis_netplay_is_running(void);
int      genesis_netplay_local_slot(void);
int      genesis_netplay_input_player(void);
uint32_t genesis_netplay_sim_tick(void);

int  genesis_netplay_start(const GenesisNetplayConfig *cfg);
void genesis_netplay_shutdown(void);

int  genesis_netplay_needs_local_sample(void);
void genesis_netplay_stage_local(uint16_t buttons);
int  genesis_netplay_poll_admit(void);
void genesis_netplay_wait_recv(int timeout_ms);
void genesis_netplay_finish_frame(void);

uint16_t genesis_netplay_published_pad(int slot);
int genesis_netplay_input_desync(uint32_t *tick, uint32_t *local_hash,
                                 uint32_t *remote_hash);
int genesis_netplay_peer_disconnected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif

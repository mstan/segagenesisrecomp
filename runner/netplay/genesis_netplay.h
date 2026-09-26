#ifndef GENESIS_NETPLAY_H
#define GENESIS_NETPLAY_H
/*
 * genesis_netplay.h -- N-seat netplay facade over recomp-net.
 *
 * Seats: 1..GENESIS_NETPLAY_MAX_SLOTS session slots, each mapped to a Genesis
 * logical player (0/1 = the two controller ports, 2/3 = adapter players such
 * as the Sonic 2 party) through slot_port[] (recomp-ui HOST_FIRST policy:
 * the lobby host is session slot 0 and plays whatever port its seat has).
 *
 * Pad blob, 4 bytes per seat (GENESIS_NETPLAY_PAD_BYTES):
 *   [0..1] LE uint16, GPAD_* bits (genesis_bus.h): 8 standard buttons +
 *          X/Y/Z/Mode, ACTIVE HIGH (1 = pressed); neutral = 0
 *   [2..3] game-defined deterministic sync bytes (slot 0 authoritative);
 *          zero unless a game registers sync hooks
 *
 * Modes: ROLLBACK (default; recomp-net's episode driver bound in
 * genesis_netplay_rb.c) or delay-sync (GENESIS_NET_MODE=delay).
 *
 * Transports:
 *   lan    two seats, rnet_session_start_lan(bind, peer)
 *   hub    three or four seats on one host: seat 0 relays
 *          (rnet_session_start_lan_hub), every other seat dials it
 *   relay  the lobby server's UDP input relay (online, 2..4 seats,
 *          spectators); the same UDP transport pointed at relay_endpoint
 *   ice    two seats online peer-to-peer (lobby WebSocket signalling)
 *
 * The simulation only ever reads PUBLISHED rows (genesis_netplay_sim_input):
 * the local controller reaches the tick through the session like everyone
 * else's (recomp-ai-rules/NETPLAY.md section 2).
 */
#include <stdint.h>

#include "sim_step.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GENESIS_NETPLAY_PAD_BYTES 4
#define GENESIS_NETPLAY_MAX_SLOTS GENESIS_SIM_MAX_PLAYERS
#define GENESIS_NETPLAY_PAD_MASK  0x0FFFu

typedef enum {
    GENESIS_NET_TRANSPORT_AUTO  = 0,
    GENESIS_NET_TRANSPORT_ICE   = 1,
    GENESIS_NET_TRANSPORT_LAN   = 2,
    GENESIS_NET_TRANSPORT_HUB   = 3,
    GENESIS_NET_TRANSPORT_RELAY = 4
} GenesisNetTransport;

typedef struct GenesisNetplayConfig {
    int      enabled;
    int      local_slot;       /* session slot, 0..slot_count-1 */
    int      slot_count;       /* seats in the match, 2..4 (1 = solo test) */
    int      spectator;        /* watch only: no seat, no input */
    int      spectator_wire_slot; /* relay namespace slot (>= slot_count) */
    int      input_player;     /* local device (logical player map) to sample */
    int      input_delay;      /* D, ticks */
    int      input_prediction; /* P, 0 = engine default */
    int      rollback;         /* 1 = rollback (default), 0 = delay-sync */
    uint32_t session_id;
    char     bind_hostport[64];
    char     peer_hostport[64];
    int      transport;        /* GenesisNetTransport */
    int      force_turn;
    uint32_t occupied_mask;    /* session slots with a player (0 = all) */
    uint8_t  slot_port[GENESIS_NETPLAY_MAX_SLOTS]; /* session slot -> player */
    int      slot_port_valid;
} GenesisNetplayConfig;

void genesis_netplay_config_defaults(GenesisNetplayConfig *cfg);
void genesis_netplay_apply_env(GenesisNetplayConfig *cfg);

int      genesis_netplay_active(void);
int      genesis_netplay_rollback_active(void);
int      genesis_netplay_is_running(void);
int      genesis_netplay_is_spectator(void);
int      genesis_netplay_local_slot(void);
int      genesis_netplay_slot_count(void);
int      genesis_netplay_input_player(void);
int      genesis_netplay_is_host(void);
uint32_t genesis_netplay_sim_tick(void);
uint32_t genesis_netplay_session_id(void);
const char *genesis_netplay_transport_name(void);

int  genesis_netplay_start(const GenesisNetplayConfig *cfg);
void genesis_netplay_shutdown(void);

/* The live tick (rollback: also runs any replay inline, first).
 *   poll_admit: 1 = a tick is admitted; run genesis_sim_step with
 *               genesis_netplay_sim_input(), then finish_frame. 0 = stall. */
int  genesis_netplay_needs_local_sample(void);
void genesis_netplay_stage_local(uint16_t buttons);
int  genesis_netplay_poll_admit(void);
void genesis_netplay_wait_recv(int timeout_ms);
void genesis_netplay_finish_frame(void);

/* The sealed GenesisSimInput of the admitted tick: published rows mapped to
 * players by slot_port[], the session-pinned pad types, and a human mask of
 * the occupied seats' players. */
void genesis_netplay_sim_input(GenesisSimInput *out);
uint16_t genesis_netplay_published_pad(int player);

/* The replayed-tick runner the rollback host calls (INLINE replay). Set by
 * the host loop (main.c): runs genesis_sim_step with resim hooks. */
typedef void (*GenesisNetplayRunTick)(const GenesisSimInput *in);
void genesis_netplay_set_tick_runner(GenesisNetplayRunTick fn);

int genesis_netplay_input_desync(uint32_t *tick, uint32_t *local_hash,
                                 uint32_t *remote_hash);
int genesis_netplay_peer_disconnected(uint32_t timeout_ms);

/* Soft return to the lobby: set on peer BYE / refusal / Escape; the host
 * loop tears the match down and reopens the waiting room (rematch). */
void genesis_netplay_request_return_to_lobby(void);
int  genesis_netplay_return_to_lobby_requested(void);
void genesis_netplay_clear_return_to_lobby(void);
/* Why the match was refused (rollback driver code), NULL if not. */
const char *genesis_netplay_refusal(void);
/* Coordinated stop (SIGUSR1): 1 once drained (or timed out). */
int  genesis_netplay_quiesced(void);
/* 1 while that stop is draining (keep polling even on a dead session). */
int  genesis_netplay_draining(void);
/* Ask for the coordinated stop from the host loop (same as SIGUSR1). */
void genesis_netplay_request_quiesce(void);

/* Session config seal (NETPLAY.md section 4: settle session-wide
 * requirements up front; session settlement is ephemeral). The settled
 * simulation-affecting configuration: port pad types, the engine widescreen
 * margin, and the game's own line (GameSpec netplay_config_image: roster,
 * characters, custom-video width...). The lobby host publishes its own in
 * match caps; every peer ADOPTS the host's for the session
 * (genesis_netplay_adopt_session_config) without persisting it. The image
 * text is then carried in the rollback mod-set handshake and folded into the
 * identity's content fingerprint, so a peer that ended up with a different
 * image REFUSES the match instead of desyncing. */
typedef struct GenesisSessionConfig {
    uint8_t pad_type[2];     /* 0 = 3-button, 1 = 6-button */
    uint8_t ws_on;           /* engine widescreen margin on */
    uint8_t ws_cells;        /* its width in 8-px cells per side */
    char    game[48];        /* the game's line (GameSpec netplay_config_image) */
    /* Game custom-video mode (GameVideo configure string): "" / "off",
     * "stage", or a window-independent ratio "W:H". The custom renderer
     * changes the simulation (Sonic 2: object activation width and the main
     * CPU divisor), so the mode is session configuration; an adaptive
     * (window-size) mode is pinned to the host's width as "W:224". */
    char    video[8];
} GenesisSessionConfig;

/* This build's own configuration (what it publishes when hosting). */
void genesis_netplay_set_local_session_config(const GenesisSessionConfig *c);
const GenesisSessionConfig *genesis_netplay_local_session_config(void);
/* Adopt the host's configuration for the next session (NULL = use local). */
void genesis_netplay_adopt_session_config(const GenesisSessionConfig *host);
/* The configuration the next/current session runs with. */
const GenesisSessionConfig *genesis_netplay_session_config(void);
/* Build the image from genesis_netplay_session_config() and seal it (call
 * before start). */
void genesis_netplay_config_seal(void);
/* Engine knobs that change the simulation but are not session settings
 * (developer environment switches, the pacing mode). They are sealed into the
 * image as-is and never adopted: two peers with different knobs refuse. */
void genesis_netplay_set_engine_knobs(const char *line);
const char *genesis_netplay_config_image(void);

/* Identity (set before start): build fingerprint = hash of the executable,
 * content fingerprint = ROM SHA-256 folded with the config image. */
void genesis_netplay_set_identity(uint32_t build_fp, const uint8_t rom_sha256[32]);

/* Host-authoritative battery SRAM (NETPLAY.md section 3). Before tick 0 the
 * host ships its SRAM image to every guest (RNET_STATE_OP_SRAM) and nobody
 * admits a tick until it has landed, so every peer boots from the host's
 * save. Guests are sandboxed: they never write a save file during a session
 * (genesis_netplay_sram_writes_allowed() is 0) and get their own SRAM back
 * afterwards. Set before start; NULL/0 = the cartridge has no SRAM. */
void genesis_netplay_set_sram(uint8_t *sram, uint32_t size);
int  genesis_netplay_sram_writes_allowed(void);

/* Diagnostics. */
void genesis_netplay_print_summary(void);

#ifdef __cplusplus
}
#endif

#endif

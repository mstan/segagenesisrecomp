#ifndef GENESIS_LOBBY_CLIENT_H
#define GENESIS_LOBBY_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GENESIS_LOBBY_ID_LEN 40
#define GENESIS_LOBBY_NAME_LEN 64
#define GENESIS_LOBBY_VERSION_LEN 32
#define GENESIS_LOBBY_ENDPOINT_LEN 64
#define GENESIS_LOBBY_MAX_LIST 32
#define GENESIS_LOBBY_MAX_MEMBERS 4

#ifndef GENESIS_GAME_VERSION
#define GENESIS_GAME_VERSION "dev"
#endif

typedef struct GenesisLobbyRow {
    char     lobby_id[GENESIS_LOBBY_ID_LEN];
    char     name[GENESIS_LOBBY_NAME_LEN];
    char     game_name[GENESIS_LOBBY_NAME_LEN];
    char     game_version[GENESIS_LOBBY_VERSION_LEN];
    int      player_count;
    int      max_slots;
    int      has_password;
} GenesisLobbyRow;

typedef struct GenesisLobbyMember {
    int  slot;
    char player_id[GENESIS_LOBBY_ID_LEN];
    char display_name[GENESIS_LOBBY_NAME_LEN];
    int  ready;
} GenesisLobbyMember;

/*
 * Host-authoritative sim settings negotiated over the lobby.
 * Guests apply these on launch so both peers boot with matching caps.
 */
typedef struct GenesisLobbyMatchCaps {
    int  valid;            /* 1 when a host blob was received / set */
    int  widescreen;       /* 0/1 */
    int  widescreen_cells; /* host-selected per-side width */
    int  pad_mode[2];      /* 0 = 3-button, 1 = 6-button */
    int  input_delay;      /* recomp-net delay frames (0-16, default 2) */
} GenesisLobbyMatchCaps;

typedef struct GenesisLobbyJoinInfo {
    int      ok;
    char     lobby_id[GENESIS_LOBBY_ID_LEN];
    uint32_t session_id;
    int      local_slot;
    char     host_endpoint[GENESIS_LOBBY_ENDPOINT_LEN];
    char     guest_endpoint[GENESIS_LOBBY_ENDPOINT_LEN];
    char     bind_hostport[GENESIS_LOBBY_ENDPOINT_LEN];
    char     peer_hostport[GENESIS_LOBBY_ENDPOINT_LEN];
    int      player_count;
    int      max_slots;
    char     last_error[64]; /* need_password | bad_password | Ã¢â‚¬Â¦ */
} GenesisLobbyJoinInfo;

/* Default URL when GENESIS_NET_LOBBY_URL unset:
 * ws://netplay.technicallycomputers.ca:8765 */
const char *genesis_lobby_default_url(void);

int  genesis_lobby_connect(const char *ws_url); /* 0 ok */
void genesis_lobby_disconnect(void);
int  genesis_lobby_connected(void);

void genesis_lobby_set_display_name(const char *name);
const char *genesis_lobby_display_name(void);
const char *genesis_lobby_player_id(void);

/* Non-blocking pump Ã¢â‚¬â€ call every frame from the launcher. */
void genesis_lobby_pump(void);

/* Title + release pin used for create/join matching and list filters. */
void genesis_lobby_set_game_identity(const char *game_name,
                                  const char *game_version);
const char *genesis_lobby_game_version(void);

void genesis_lobby_request_list(void);
int  genesis_lobby_list_count(void);
int  genesis_lobby_list_get(int index, GenesisLobbyRow *out);

/*
 * Create lobby. host_bind e.g. "0.0.0.0:7777". password may be NULL/empty.
 * match_caps may be NULL (legacy); when non-NULL and valid, sent to the server
 * so guests join with the host's sim settings.
 * Returns 0 if request sent; poll genesis_lobby_join_info() / in_lobby().
 */
int  genesis_lobby_create(const char *name, const char *game_name,
                      const char *game_version, const char *password,
                      const char *host_bind,
                      const GenesisLobbyMatchCaps *match_caps);

int  genesis_lobby_join(const char *lobby_id, const char *password,
                    const char *guest_bind);

int  genesis_lobby_leave(void);

int  genesis_lobby_in_lobby(void);
int  genesis_lobby_is_host(void);
/* Filled after create/join/lobby_update; peer endpoints for GenesisNetplayConfig. */
const GenesisLobbyJoinInfo *genesis_lobby_join_info(void);

/* Latest host match_caps (valid==0 until create/join/launch delivers one). */
const GenesisLobbyMatchCaps *genesis_lobby_match_caps(void);

/* Host: push updated caps while in lobby (clears ready via lobby_update). */
int  genesis_lobby_set_match_caps(const GenesisLobbyMatchCaps *caps);

/* Live member table from lobby_update (and create/join). */
int  genesis_lobby_member_count(void);
int  genesis_lobby_member_get(int index, GenesisLobbyMember *out);

/* Local ready flag (from last lobby_update matching our player_id). */
int  genesis_lobby_local_ready(void);
/* True when every seated player is ready and player_count >= 2. */
int  genesis_lobby_all_ready(void);

/* Toggle ready in the current lobby. */
int  genesis_lobby_set_ready(int ready);

/*
 * Host: ask server to broadcast launch. When match_caps is non-NULL and valid,
 * it is attached to start so launch freezes the latest host settings.
 */
int  genesis_lobby_request_start(const GenesisLobbyMatchCaps *match_caps);

/*
 * Set when server sends op:launch. Both host and guests should boot netplay.
 * Cleared by genesis_lobby_clear_launch_pending() after consuming.
 */
int  genesis_lobby_launch_pending(void);
void genesis_lobby_clear_launch_pending(void);

/*
 * ICE signaling relay (MotK WS op:signal). text is SDP/candidate (max 2047).
 * send returns 0 if queued/written; poll returns 1 when an inbound signal was
 * copied out (LOCAL_* types as emitted by the peer Ã¢â‚¬â€ remap to REMOTE_* before
 * rnet_session_push_signal).
 */
int  genesis_lobby_send_signal(int type, int flag, const char *text);
int  genesis_lobby_poll_signal(int *type, int *flag, char *text, size_t text_cap);

#ifdef __cplusplus
}
#endif

#endif /* GENESIS_LOBBY_CLIENT_H */

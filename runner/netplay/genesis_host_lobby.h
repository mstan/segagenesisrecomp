#ifndef GENESIS_HOST_LOBBY_H
#define GENESIS_HOST_LOBBY_H
/*
 * genesis_host_lobby.h -- the Genesis adapter over recomp-ui's shared netplay
 * backend (recomp_netplay_host.h). Replaces runner/lobby (a 1.3k-line
 * NES-derived client) and genesis_launcher_netplay.c: the lobby, rooms, LAN,
 * automatch, mod plan and launch filling are recomp-ui's; what is left is what
 * only the Genesis host knows -- identity, seats, the session config image
 * in match caps, the fork report, and the headless room selftest.
 *
 * Session slots follow recomp-ui's HOST_FIRST policy: the lobby host is
 * session slot 0 and launch.slot_port[] maps each session slot to the
 * logical player of its lobby seat (genesis_netplay honours it).
 */
#include <stdint.h>

#include "recomp_launcher.h"
#include "genesis_netplay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GenesisHostLobbyIdentity {
    const char *game_name;        /* server scoping key */
    const char *release_version;  /* e.g. "0.7.0"; the lobby version adds the exe hash */
    const char *rom_sha256_hex;   /* 64 hex chars */
    const char *lan_registry_path;
    int         max_players;      /* 2..4 */
} GenesisHostLobbyIdentity;

int  genesis_host_lobby_init(const GenesisHostLobbyIdentity *id);
const RecompLauncherCNetplayCallbacks *genesis_host_lobby_callbacks(void);

/* A launch (launcher PLAY or headless selftest) -> the netplay config, with
 * the host's session config adopted. Returns 0, or -1 when this peer cannot
 * honour the host's configuration (last_error is set). */
int  genesis_host_lobby_config_from_launch(const RecompLauncherCNetplayLaunch *launch,
                                           GenesisNetplayConfig *cfg);

/* The host's session configuration, as this peer received it in match caps
 * (NULL when the launch carried none: LAN rooms, older hosts). */
const char *genesis_host_lobby_host_config(void);

void genesis_host_lobby_begin_soft_return(RecompLauncherCGameInfo *gi);
void genesis_host_lobby_prepare_rematch(void);
void genesis_host_lobby_set_runtime_error(const char *code);

/* Headless room (GENESIS_LOBBY_SELFTEST=host|guest): round 1 creates or
 * joins GENESIS_LOBBY_SELFTEST_LOBBY and waits for
 * GENESIS_LOBBY_SELFTEST_PLAYERS members; later rounds are the waiting room
 * after a soft return (ready, start, launch). 0 = *out filled. */
int  genesis_host_lobby_selftest_role(void);   /* 0 none, 1 host, 2 guest */
int  genesis_host_lobby_selftest_room(int round, RecompLauncherCNetplayLaunch *out);
void genesis_host_lobby_selftest_report_room(int round);

#ifdef __cplusplus
}
#endif

#endif

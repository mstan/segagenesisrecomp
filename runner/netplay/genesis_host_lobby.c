/*
 * genesis_host_lobby.c -- Genesis adapter over recomp-ui's recomp_netplay_host
 * (see genesis_host_lobby.h). Shape copied from snesrecomp's
 * runner/src/netplay/snes_host_lobby.c.
 */
#include "genesis_host_lobby.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_netplay_host.h"
#include "recomp_net/lobby_client.h"
#include "genesis_netplay_identity.h"
#include "genesis_netplay_rb.h"

#if defined(_WIN32)
#include <windows.h>
static void hl_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void hl_sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif
#include "retcomm_rbengine/mono_ms.h"

/* ---- the session config in match caps -------------------------------------
 * The host publishes its GenesisSessionConfig as four engine keys; every peer
 * parses them into caps->ext and adopts them for the launch. 64 bytes of ext:
 * the struct is 60. */
_Static_assert(sizeof(GenesisSessionConfig) + 1 <= RNET_LOBBY_CAPS_EXT_BYTES,
               "GenesisSessionConfig no longer fits RNetLobbyMatchCaps.ext");
typedef struct GenesisCapsExt {
    uint8_t valid;
    GenesisSessionConfig cfg;
} GenesisCapsExt;

static GenesisSessionConfig s_host_cfg;
static int s_host_cfg_valid;
static char s_host_cfg_text[160];

static int caps_write(const RNetLobbyMatchCaps *caps, char *out, size_t cap, void *ctx)
{
    const GenesisCapsExt *e = (const GenesisCapsExt *)caps->ext.bytes;
    char esc[2 * sizeof e->cfg.game + 8];
    (void)ctx;
    if (!e->valid) return 0;
    rnet_lobby_json_escape(e->cfg.game, esc, sizeof esc);
    char vesc[2 * sizeof e->cfg.video + 8];
    rnet_lobby_json_escape(e->cfg.video, vesc, sizeof vesc);
    int n = snprintf(out, cap, ",\"gen_pads\":\"%u%u\",\"gen_ws\":%u,\"gen_cells\":%u,\"gen_game\":\"%s\",\"gen_video\":\"%s\"",
                     (unsigned)e->cfg.pad_type[0], (unsigned)e->cfg.pad_type[1],
                     (unsigned)e->cfg.ws_on, (unsigned)e->cfg.ws_cells, esc, vesc);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

static void caps_parse(const char *json, RNetLobbyMatchCaps *caps, void *ctx)
{
    GenesisCapsExt *e = (GenesisCapsExt *)caps->ext.bytes;
    char pads[8] = "";
    (void)ctx;
    memset(e, 0, sizeof *e);
    if (!rnet_lobby_json_get_str(json, "gen_pads", pads, sizeof pads) || strlen(pads) != 2)
        return;
    e->cfg.pad_type[0] = pads[0] == '1';
    e->cfg.pad_type[1] = pads[1] == '1';
    e->cfg.ws_on = (uint8_t)(rnet_lobby_json_get_int(json, "gen_ws", 0) != 0);
    e->cfg.ws_cells = (uint8_t)rnet_lobby_json_get_int(json, "gen_cells", 0);
    rnet_lobby_json_get_str(json, "gen_game", e->cfg.game, sizeof e->cfg.game);
    rnet_lobby_json_get_str(json, "gen_video", e->cfg.video, sizeof e->cfg.video);
    e->valid = 1;
}

static const RNetLobbyCapsCodec k_codec = { caps_write, caps_parse, NULL };

static void fill_caps(void *ctx, const RecompLauncherCSettings *settings, RNetLobbyMatchCaps *caps)
{
    GenesisCapsExt *e = (GenesisCapsExt *)caps->ext.bytes;
    (void)ctx; (void)settings;
    memset(e, 0, sizeof *e);
    e->valid = 1;
    /* The host publishes its OWN configuration, never one it adopted. */
    e->cfg = *genesis_netplay_session_config();
}

static void apply_caps(void *ctx, const RNetLobbyMatchCaps *caps, RecompLauncherCNetplayLaunch *launch)
{
    const GenesisCapsExt *e = (const GenesisCapsExt *)caps->ext.bytes;
    (void)ctx; (void)launch;
    s_host_cfg_valid = e->valid;
    if (e->valid) {
        s_host_cfg = e->cfg;
        s_host_cfg.game[sizeof s_host_cfg.game - 1] = 0;
        s_host_cfg.video[sizeof s_host_cfg.video - 1] = 0;
        snprintf(s_host_cfg_text, sizeof s_host_cfg_text, "pad=%u,%u ws=%u cells=%u video=%s game=%s",
                 (unsigned)s_host_cfg.pad_type[0], (unsigned)s_host_cfg.pad_type[1],
                 (unsigned)s_host_cfg.ws_on, (unsigned)s_host_cfg.ws_cells,
                 s_host_cfg.video[0] ? s_host_cfg.video : "off", s_host_cfg.game);
    }
}

const char *genesis_host_lobby_host_config(void) { return s_host_cfg_valid ? s_host_cfg_text : NULL; }

/* ---- host environment ----------------------------------------------------- */

static int last_fork(void *ctx, uint32_t *tick, const char **partition, uint32_t *mine, uint32_t *theirs)
{
    (void)ctx;
    return genesis_netplay_rb_last_fork(tick, partition, mine, theirs);
}

static char s_version[64];
static char s_sha[65];

int genesis_host_lobby_init(const GenesisHostLobbyIdentity *id)
{
    RecompNetplayHostHooks h;
    if (!id || !id->game_name || !id->game_name[0]) return -1;
    snprintf(s_version, sizeof s_version, "%s", genesis_identity_game_version(id->release_version));
    snprintf(s_sha, sizeof s_sha, "%s", id->rom_sha256_hex ? id->rom_sha256_hex : "");
    memset(&h, 0, sizeof h);
    h.game_name = id->game_name;
    h.game_version = s_version;
    h.content_fingerprint = s_sha[0] ? s_sha : NULL;
    h.lan_registry_path = id->lan_registry_path;
    h.platform = "genesis";
    h.legacy_env_prefix = "GENESIS_NET_";
    h.max_players = id->max_players < 2 ? 2 : id->max_players > 4 ? 4 : id->max_players;
    h.slot_policy = RECOMP_NETPLAY_SLOTS_HOST_FIRST;
    h.input_player = 0;
    h.caps_codec = &k_codec;
    h.fill_match_caps = fill_caps;
    h.apply_match_caps = apply_caps;
    h.last_fork = last_fork;
    h.auto_ready_guests = 1;
    h.rematch_set_ready = 1;
    fprintf(stderr, "genesis_netplay: lobby identity game=\"%s\" version=%s content=%s seats=%d\n",
            id->game_name, s_version, s_sha[0] ? s_sha : "(unknown)", h.max_players);
    return recomp_netplay_host_init(&h);
}

const RecompLauncherCNetplayCallbacks *genesis_host_lobby_callbacks(void)
{
    return recomp_netplay_host_callbacks();
}

int genesis_host_lobby_config_from_launch(const RecompLauncherCNetplayLaunch *l, GenesisNetplayConfig *cfg)
{
    if (!l || !cfg || !l->enabled) return -1;
    cfg->enabled = 1;
    cfg->spectator = l->is_spectator ? 1 : 0;
    cfg->spectator_wire_slot = l->spectator_wire_slot;
    cfg->local_slot = l->local_slot;
    cfg->slot_count = l->player_count > 0 ? l->player_count : (l->max_slots > 0 ? l->max_slots : 2);
    cfg->input_player = l->input_player >= 0 ? l->input_player : 0;
    cfg->input_delay = l->input_delay > 0 ? l->input_delay : 2;
    cfg->input_prediction = l->input_prediction;
    /* A LAN room settles no mode (docs/HOST_NETPLAY.md); rollback stays the
     * Genesis default there, and GENESIS_NET_MODE still overrides. */
    cfg->rollback = recomp_netplay_host_in_lan() ? cfg->rollback : (l->rollback ? 1 : 0);
    cfg->session_id = l->session_id;
    cfg->force_turn = l->force_turn;
    cfg->occupied_mask = l->occupied_mask;
    snprintf(cfg->bind_hostport, sizeof cfg->bind_hostport, "%s", l->bind_hostport);
    snprintf(cfg->peer_hostport, sizeof cfg->peer_hostport, "%s", l->peer_hostport);
    /* Online launches ride the lobby server's UDP input relay (the SFU):
     * the one transport that carries 3-4 players and spectators. */
    cfg->transport = recomp_netplay_host_in_lan() ? GENESIS_NET_TRANSPORT_LAN
                                                  : GENESIS_NET_TRANSPORT_RELAY;
    if (l->slot_port_valid) {
        cfg->slot_port_valid = 1;
        for (int i = 0; i < GENESIS_NETPLAY_MAX_SLOTS; i++) {
            int p = l->slot_port[i];
            cfg->slot_port[i] = (uint8_t)(p >= 0 && p < GENESIS_NETPLAY_MAX_SLOTS ? p : i);
        }
    }
    /* Adopt the host's session configuration (ephemeral: never persisted). */
    genesis_netplay_adopt_session_config(s_host_cfg_valid ? &s_host_cfg : NULL);
    return 0;
}

void genesis_host_lobby_begin_soft_return(RecompLauncherCGameInfo *gi)
{
    recomp_netplay_host_begin_soft_return(gi, 1);
}

void genesis_host_lobby_prepare_rematch(void) { recomp_netplay_host_prepare_rematch(); }
void genesis_host_lobby_set_runtime_error(const char *code) { recomp_netplay_host_set_runtime_error(code); }

/* ---- headless room ------------------------------------------------------------ */

int genesis_host_lobby_selftest_role(void)
{
    const char *r = getenv("GENESIS_LOBBY_SELFTEST");
    if (!r) return 0;
    if (!strcmp(r, "host")) return 1;
    if (!strcmp(r, "guest")) return 2;
    return 0;
}

static const char *env_or(const char *k, const char *d) { const char *v = getenv(k); return v && v[0] ? v : d; }

void genesis_host_lobby_selftest_report_room(int round)
{
    const RecompLauncherCNetplayCallbacks *cb = recomp_netplay_host_callbacks();
    int role = genesis_host_lobby_selftest_role();
    if (!role || !cb) return;
    for (int i = 0; i < 20; i++) { cb->pump(NULL); hl_sleep_ms(10); }
    const char *e = cb->last_error ? cb->last_error(NULL) : NULL;
    fprintf(stderr, "[lobby-selftest] %s after match %d back in the room: in_lobby=%d is_host=%d "
                    "members=%d last_error=\"%s\"\n", role == 1 ? "host" : "guest", round - 1,
            cb->in_lobby(NULL), cb->is_host(NULL), cb->member_count(NULL), e ? e : "");
}

int genesis_host_lobby_selftest_room(int round, RecompLauncherCNetplayLaunch *out)
{
    const RecompLauncherCNetplayCallbacks *cb = recomp_netplay_host_callbacks();
    int role = genesis_host_lobby_selftest_role();
    int is_host = role == 1;
    const char *rname = is_host ? "host" : "guest";
    int players = atoi(env_or("GENESIS_LOBBY_SELFTEST_PLAYERS", "2"));
    int spectators = atoi(env_or("GENESIS_LOBBY_SELFTEST_SPECTATORS", "0"));
    int lan = atoi(env_or("GENESIS_LOBBY_SELFTEST_LAN", "0"));
    int lan_port = atoi(env_or("GENESIS_LOBBY_SELFTEST_LAN_PORT", "17790"));
    const char *lobby = env_or("GENESIS_LOBBY_SELFTEST_LOBBY", "genesis-selftest");
    const char *name = env_or("GENESIS_LOBBY_SELFTEST_NAME", is_host ? "HostTest" : "GuestTest");
    uint32_t deadline = rbe_mono_ms() + 90000u;
    RecompLauncherCSettings settings;
    int joined = 0, ready_sent = 0, started = 0;
    uint32_t next_list = 0;

    if (!role || !cb || !out) return -1;
    if (players < 2) players = 2;
    if (players > 4) players = 4;
    memset(out, 0, sizeof *out);
    memset(&settings, 0, sizeof settings);

    if (round == 1) {
        cb->set_player_name(NULL, name);
        if (!lan) {
            if (cb->connect(NULL) != 0) { fprintf(stderr, "[lobby-selftest] %s connect failed\n", rname); return -2; }
            while (!rnet_lobby_player_id()[0]) {
                cb->pump(NULL);
                if (!cb->connected(NULL) || rbe_mono_ms() > deadline) {
                    fprintf(stderr, "[lobby-selftest] %s no welcome\n", rname);
                    return -3;
                }
                hl_sleep_ms(10);
            }
        }
        if (is_host) {
            char ep[64];
            if (spectators > 0 && cb->allow_spectators_set) cb->allow_spectators_set(NULL, 1);
            snprintf(ep, sizeof ep, lan ? "127.0.0.1:%d" : "0.0.0.0:%d", lan ? lan_port : 7777);
            int rc = cb->create(NULL, lobby, ep, "", &settings, lan ? 1 : 0, lan ? 2 : players);
            fprintf(stderr, "[lobby-selftest] host round=1 %s create rc=%d seats=%d\n",
                    lan ? "lan" : "online", rc, lan ? 2 : players);
            if (rc != 0) return -5;
            joined = 1;
        } else if (lan) {
            char gb[64], id[80];
            snprintf(gb, sizeof gb, "0.0.0.0:%d", lan_port + 1);
            snprintf(id, sizeof id, "lan:127.0.0.1:%d", lan_port);
            int rc = -1;
            while (rbe_mono_ms() < deadline && (rc = cb->join(NULL, id, "", gb)) != 0) hl_sleep_ms(250);
            fprintf(stderr, "[lobby-selftest] guest round=1 lan join rc=%d\n", rc);
            if (rc != 0) return -7;
            joined = 1;
        }
    } else {
        joined = 1;
    }
    while (rbe_mono_ms() < deadline) {
        cb->pump(NULL);
        if (!is_host && !joined && rbe_mono_ms() >= next_list) {
            cb->request_list(NULL);
            next_list = rbe_mono_ms() + 500u;
        }
        if (!is_host && !joined) {
            RecompLauncherCNetplayLobby row;
            for (int i = 0; i < cb->list_count(NULL); i++) {
                if (cb->list_get(NULL, i, &row) && !strcmp(row.name, lobby)) {
                    char gb[64] = "";
                    if (cb->join(NULL, row.lobby_id, "", gb) == 0) {
                        joined = 1;
                        fprintf(stderr, "[lobby-selftest] guest round=%d join sent\n", round);
                    }
                    break;
                }
            }
        }
        int need = lan ? 2 : players + spectators;
        if (joined && cb->in_lobby(NULL) && !ready_sent && (!is_host || cb->member_count(NULL) >= need))
            ready_sent = cb->set_ready(NULL, 1) == 0;
        if (is_host && !started && cb->member_count(NULL) >= need && cb->all_ready(NULL)) {
            started = cb->request_start(NULL, &settings) == 0;
            fprintf(stderr, "[lobby-selftest] host round=%d start=%d members=%d\n", round, started,
                    cb->member_count(NULL));
        }
        if (cb->launch_pending(NULL)) {
            int ok = cb->fill_launch(NULL, out);
            fprintf(stderr, "[lobby-selftest] %s round=%d fill_launch=%d slot=%d players=%d "
                            "session=%u bind=%s peer=%s occupied=%x spectator=%d wire=%d\n", rname, round, ok,
                    out->local_slot, out->player_count, (unsigned)out->session_id,
                    out->bind_hostport, out->peer_hostport, (unsigned)out->occupied_mask,
                    out->is_spectator, out->spectator_wire_slot);
            return ok ? 0 : -11;
        }
        hl_sleep_ms(10);
    }
    {
        const RNetLobbyJoinInfo *ji = rnet_lobby_join_info();
        fprintf(stderr, "[lobby-selftest] %s round=%d TIMED OUT waiting for a launch "
                        "(in_lobby=%d members=%d all_ready=%d session=%u join_error=\"%s\" room_error=\"%s\")\n",
                rname, round, cb->in_lobby(NULL), cb->member_count(NULL), cb->all_ready(NULL),
                ji ? (unsigned)ji->session_id : 0u, ji ? ji->last_error : "",
                cb->last_error && cb->last_error(NULL) ? cb->last_error(NULL) : "");
    }
    return -10;
}

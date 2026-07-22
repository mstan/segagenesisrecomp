#include "genesis_netplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_net/recomp_net.h"

#if defined(GENESIS_HAS_LOBBY_CLIENT)
#include "genesis_lobby_client.h"
#endif

typedef struct GenesisNetplayState {
    RNetSession *session;
    uint16_t staged_buttons;
    uint16_t published[2];
    uint32_t latched_sim_tick;
    int staged_valid;
    int active;
    int local_slot;
    int input_player;
    int needs_advance;
    int latched_for_tick;
    int use_ice;
} GenesisNetplayState;

static GenesisNetplayState g_np;

static unsigned env_u(const char *name, unsigned fallback)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long parsed;
    if (!value || !value[0]) return fallback;
    parsed = strtoul(value, &end, 0);
    return end && end != value ? (unsigned)parsed : fallback;
}

void genesis_netplay_config_defaults(GenesisNetplayConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->local_slot = 0;
    cfg->input_player = 0;
    cfg->input_delay = 2;
    cfg->session_id = 1;
    strcpy(cfg->bind_hostport, "0.0.0.0:7777");
}

void genesis_netplay_apply_env(GenesisNetplayConfig *cfg)
{
    const char *value;
    if (!cfg) return;
    value = getenv("GENESIS_NETPLAY");
    if (value) cfg->enabled = atoi(value) != 0;
    value = getenv("GENESIS_NET_SLOT");
    if (value) cfg->local_slot = atoi(value);
    value = getenv("GENESIS_NET_INPUT_PLAYER");
    if (value) cfg->input_player = atoi(value);
    value = getenv("GENESIS_NET_DELAY");
    if (value) cfg->input_delay = atoi(value);
    cfg->session_id = env_u("GENESIS_NET_SESSION_ID", cfg->session_id);
    value = getenv("GENESIS_NET_BIND");
    if (value && value[0]) {
        strncpy(cfg->bind_hostport, value, sizeof(cfg->bind_hostport) - 1);
        cfg->bind_hostport[sizeof(cfg->bind_hostport) - 1] = '\0';
    }
    value = getenv("GENESIS_NET_PEER");
    if (value && value[0]) {
        strncpy(cfg->peer_hostport, value, sizeof(cfg->peer_hostport) - 1);
        cfg->peer_hostport[sizeof(cfg->peer_hostport) - 1] = '\0';
    }
    value = getenv("GENESIS_NET_TRANSPORT");
    if (value) {
        if (!strcmp(value, "ice")) cfg->transport = 1;
        else if (!strcmp(value, "lan")) cfg->transport = 2;
    }
}

static void encode_pad(uint16_t buttons, RNetInputSample *out, rnet_u32 tick)
{
    memset(out, 0, sizeof(*out));
    out->tick = tick;
    out->size = 2;
    out->bytes[0] = (rnet_u8)(buttons & 0xffu);
    out->bytes[1] = (rnet_u8)(buttons >> 8);
    out->valid = 1;
}

static uint16_t decode_pad(const RNetInputSample *sample)
{
    if (!sample || !sample->valid || sample->size < 2) return 0;
    return (uint16_t)sample->bytes[0] | ((uint16_t)sample->bytes[1] << 8);
}

static void host_sample_local(rnet_u32 tick, RNetInputSample *out, void *ctx)
{
    GenesisNetplayState *state = (GenesisNetplayState *)ctx;
    encode_pad(state->staged_valid ? state->staged_buttons : 0, out, tick);
}

static void host_publish(rnet_u32 tick, const RNetInputSample *by_slot,
                         int slots, void *ctx)
{
    GenesisNetplayState *state = (GenesisNetplayState *)ctx;
    int i;
    (void)tick;
    state->published[0] = state->published[1] = 0;
    for (i = 0; by_slot && i < slots && i < 2; ++i)
        state->published[i] = decode_pad(&by_slot[i]) & 0x0fffu;
}

#if defined(GENESIS_HAS_LOBBY_CLIENT) && defined(RNET_ENABLE_ICE)
static void host_on_signal(const RNetSignal *message, void *ctx)
{
    (void)ctx;
    if (message)
        (void)genesis_lobby_send_signal((int)message->type,
                                        (int)message->flag, message->text);
}

static void drain_lobby_signals(void)
{
    int type = 0, flag = 0;
    char text[2048];
    while (g_np.session &&
           genesis_lobby_poll_signal(&type, &flag, text, sizeof(text))) {
        RNetSignal signal;
        memset(&signal, 0, sizeof(signal));
        if (type == (int)RNET_SIGNAL_LOCAL_SDP)
            type = (int)RNET_SIGNAL_REMOTE_SDP;
        else if (type == (int)RNET_SIGNAL_LOCAL_CANDIDATE)
            type = (int)RNET_SIGNAL_REMOTE_CANDIDATE;
        signal.type = (RNetSignalType)type;
        signal.flag = (rnet_u8)(flag & 0xff);
        strncpy(signal.text, text, sizeof(signal.text) - 1);
        rnet_session_push_signal(g_np.session, &signal);
    }
}
#else
static void drain_lobby_signals(void) {}
#endif

static int hostport_is_private(const char *hostport)
{
    char host[64];
    const char *colon;
    size_t n;
    unsigned a = 0, b = 0;
    if (!hostport || !hostport[0]) return 1;
    colon = strrchr(hostport, ':');
    n = colon ? (size_t)(colon - hostport) : strlen(hostport);
    if (n >= sizeof(host)) n = sizeof(host) - 1;
    memcpy(host, hostport, n);
    host[n] = '\0';
    if (!strcmp(host, "localhost")) return 1;
    if (sscanf(host, "%u.%u", &a, &b) < 1) return 0;
    return a == 127 || a == 10 || (a == 192 && b == 168) ||
           (a == 172 && b >= 16 && b <= 31);
}

static int resolve_use_ice(const GenesisNetplayConfig *cfg)
{
    if (cfg->transport == 2) return 0;
#if defined(RNET_ENABLE_ICE) && defined(GENESIS_HAS_LOBBY_CLIENT)
    if (!genesis_lobby_connected() || !genesis_lobby_in_lobby()) return 0;
    if (cfg->transport == 1) return 1;
    return !hostport_is_private(cfg->peer_hostport);
#else
    (void)cfg;
    return 0;
#endif
}

int genesis_netplay_active(void) { return g_np.active && g_np.session; }
int genesis_netplay_is_running(void)
{
    return genesis_netplay_active() && rnet_session_is_running(g_np.session);
}
int genesis_netplay_local_slot(void) { return genesis_netplay_active() ? g_np.local_slot : -1; }
int genesis_netplay_input_player(void) { return genesis_netplay_active() ? g_np.input_player : 0; }
uint32_t genesis_netplay_sim_tick(void)
{
    return genesis_netplay_active() ? rnet_session_sim_tick(g_np.session) : 0;
}

int genesis_netplay_start(const GenesisNetplayConfig *cfg)
{
    RNetConfig net_cfg;
    RNetHostVTable host;
    int use_ice;
    if (!cfg || !cfg->enabled) return -1;
    genesis_netplay_shutdown();
    rnet_config_init_defaults(&net_cfg);
    net_cfg.slot_count = 2;
    net_cfg.local_slot = (rnet_u8)(cfg->local_slot == 1 ? 1 : 0);
    net_cfg.input_delay = (rnet_u8)(cfg->input_delay < 0 ? 0 :
                                    cfg->input_delay > 16 ? 16 : cfg->input_delay);
    net_cfg.session_id = cfg->session_id ? cfg->session_id : 1;
    memset(&host, 0, sizeof(host));
    host.sample_local = host_sample_local;
    host.publish = host_publish;
    host.ctx = &g_np;
    use_ice = resolve_use_ice(cfg);
#if defined(GENESIS_HAS_LOBBY_CLIENT) && defined(RNET_ENABLE_ICE)
    if (use_ice) host.on_signal = host_on_signal;
#endif
    g_np.session = rnet_session_create(&net_cfg, &host);
    if (!g_np.session) return -2;
    if (use_ice) {
#if defined(RNET_ENABLE_ICE)
        RNetIceConfig ice;
        rnet_ice_config_init_defaults(&ice);
        ice.controlling = net_cfg.local_slot == 0;
        if (rnet_session_start_ice(g_np.session, &ice) != 0) {
            rnet_session_destroy(g_np.session);
            g_np.session = NULL;
            return -3;
        }
#endif
    } else if (rnet_session_start_lan(g_np.session, cfg->bind_hostport,
                                      cfg->peer_hostport) != 0) {
        rnet_session_destroy(g_np.session);
        g_np.session = NULL;
        return -3;
    }
    g_np.active = 1;
    g_np.local_slot = net_cfg.local_slot;
    g_np.input_player = cfg->input_player == 1 ? 1 : 0;
    g_np.use_ice = use_ice;
    fprintf(stderr, "genesis_netplay: started transport=%s slot=%d input=%d "
                    "session=%u delay=%u bind=%s peer=%s\n",
            use_ice ? "ice" : "lan", g_np.local_slot, g_np.input_player,
            (unsigned)net_cfg.session_id, (unsigned)net_cfg.input_delay,
            cfg->bind_hostport, cfg->peer_hostport);
    return 0;
}

void genesis_netplay_shutdown(void)
{
    if (g_np.session) {
        (void)rnet_session_send_bye(g_np.session);
        rnet_session_destroy(g_np.session);
    }
    memset(&g_np, 0, sizeof(g_np));
}

int genesis_netplay_needs_local_sample(void)
{
    uint32_t tick;
    if (!genesis_netplay_active()) return 0;
    if (!rnet_session_is_running(g_np.session)) return 1;
    tick = rnet_session_sim_tick(g_np.session);
    return !g_np.latched_for_tick || g_np.latched_sim_tick != tick;
}

void genesis_netplay_stage_local(uint16_t buttons)
{
    uint32_t tick;
    if (!genesis_netplay_active()) return;
    tick = rnet_session_sim_tick(g_np.session);
    if (g_np.latched_for_tick && g_np.latched_sim_tick == tick) return;
    g_np.staged_buttons = buttons & 0x0fffu;
    g_np.staged_valid = 1;
    g_np.latched_for_tick = 1;
    g_np.latched_sim_tick = tick;
}

int genesis_netplay_poll_admit(void)
{
    uint32_t tick;
    if (!genesis_netplay_active()) return 1;
#if defined(GENESIS_HAS_LOBBY_CLIENT)
    if (g_np.use_ice || genesis_lobby_connected()) genesis_lobby_pump();
#endif
    drain_lobby_signals();
    rnet_session_pump(g_np.session);
    if (!rnet_session_is_running(g_np.session)) return 0;
    if (g_np.needs_advance) return 1;
    tick = rnet_session_sim_tick(g_np.session);
    if (!rnet_session_try_admit(g_np.session, tick)) return 0;
    g_np.needs_advance = 1;
    return 1;
}

void genesis_netplay_wait_recv(int timeout_ms)
{
    if (!genesis_netplay_active()) return;
    (void)rnet_session_wait_recv(g_np.session, timeout_ms);
}

void genesis_netplay_finish_frame(void)
{
    if (!genesis_netplay_active() || !g_np.needs_advance) return;
    rnet_session_advance(g_np.session);
    g_np.needs_advance = 0;
    g_np.latched_for_tick = 0;
}

uint16_t genesis_netplay_published_pad(int slot)
{
    return slot >= 0 && slot < 2 ? g_np.published[slot] : 0;
}

int genesis_netplay_input_desync(uint32_t *tick, uint32_t *local_hash,
                                 uint32_t *remote_hash)
{
    return genesis_netplay_active() &&
           rnet_session_input_desync(g_np.session, tick, local_hash, remote_hash);
}

int genesis_netplay_peer_disconnected(uint32_t timeout_ms)
{
    if (!genesis_netplay_active()) return 0;
    return rnet_session_peer_disconnected(g_np.session,
                                           timeout_ms ? timeout_ms : 1500);
}

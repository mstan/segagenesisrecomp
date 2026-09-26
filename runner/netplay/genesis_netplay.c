/* genesis_netplay.c -- N-seat netplay facade over recomp-net (see .h). */
#include "genesis_netplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_net/recomp_net.h"
#include "recomp_net/lobby_client.h"
#include "genesis_netplay_rb.h"

#if defined(_WIN32)
#include <windows.h>
static void np_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void np_sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

typedef struct GenesisNetplayState {
    RNetSession *session;
    GenesisNetplayConfig cfg;
    uint16_t staged_buttons;
    int      staged_valid;
    int      active;
    int      rollback;           /* 1 once the rollback host took admission */
    int      slot_count;
    int      local_slot;
    int      spectator;
    int      input_player;
    int      input_delay;
    int      input_prediction;
    int      needs_advance;
    int      latched_for_tick;
    uint32_t latched_sim_tick;
    uint16_t published[GENESIS_NETPLAY_MAX_SLOTS];   /* by session slot */
    uint8_t  slot_port[GENESIS_NETPLAY_MAX_SLOTS];
    uint32_t occupied_mask;
    uint8_t  pad_type[2];
    int      transport;          /* resolved GenesisNetTransport */
    uint32_t frames_finished;
    /* Validation only (GENESIS_NET_TEST_PAD, harness): a scripted local pad
     * "<period>[:<hold>[:<mask>]]" so a follower produces organic, guest-
     * visible input changes the peer has to predict. */
    uint32_t test_period, test_hold;
    uint16_t test_mask;
    int      sram_sync_sent, sram_sync_done;
} GenesisNetplayState;

static uint8_t *g_sram;
static uint32_t g_sram_size;
void genesis_netplay_set_sram(uint8_t *sram, uint32_t size) { g_sram = sram; g_sram_size = sram ? size : 0; }
int  genesis_netplay_sram_writes_allowed(void) { return !genesis_netplay_active() || genesis_netplay_is_host(); }

static GenesisNetplayState g_np;
static int g_return_to_lobby;
static GenesisNetplayRunTick g_tick_runner;

/* ---- env ------------------------------------------------------------------ */

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
    cfg->slot_count = 2;
    cfg->input_delay = 2;
    cfg->rollback = 1;          /* rollback is the default mode */
    cfg->session_id = 1;
    strcpy(cfg->bind_hostport, "0.0.0.0:7777");
    for (int i = 0; i < GENESIS_NETPLAY_MAX_SLOTS; i++) cfg->slot_port[i] = (uint8_t)i;
}

void genesis_netplay_apply_env(GenesisNetplayConfig *cfg)
{
    const char *v;
    if (!cfg) return;
    if ((v = getenv("GENESIS_NETPLAY"))) cfg->enabled = atoi(v) != 0;
    if ((v = getenv("GENESIS_NET_SLOT"))) cfg->local_slot = atoi(v);
    if ((v = getenv("GENESIS_NET_SLOTS"))) cfg->slot_count = atoi(v);
    if ((v = getenv("GENESIS_NET_SPECTATOR"))) cfg->spectator = atoi(v) != 0;
    if ((v = getenv("GENESIS_NET_SPECTATOR_WIRE_SLOT"))) cfg->spectator_wire_slot = atoi(v);
    if ((v = getenv("GENESIS_NET_INPUT_PLAYER"))) cfg->input_player = atoi(v);
    if ((v = getenv("GENESIS_NET_DELAY"))) cfg->input_delay = atoi(v);
    if ((v = getenv("GENESIS_NET_PREDICTION"))) cfg->input_prediction = atoi(v);
    if ((v = getenv("GENESIS_NET_MODE")) && v[0]) {
        if (!strcmp(v, "rollback") || !strcmp(v, "rb")) cfg->rollback = 1;
        else if (!strcmp(v, "delay")) cfg->rollback = 0;
    }
    cfg->session_id = env_u("GENESIS_NET_SESSION_ID", cfg->session_id);
    cfg->occupied_mask = env_u("GENESIS_NET_OCCUPIED", cfg->occupied_mask);
    if ((v = getenv("GENESIS_NET_BIND")) && v[0])
        snprintf(cfg->bind_hostport, sizeof cfg->bind_hostport, "%s", v);
    if ((v = getenv("GENESIS_NET_PEER")) && v[0])
        snprintf(cfg->peer_hostport, sizeof cfg->peer_hostport, "%s", v);
    if ((v = getenv("GENESIS_NET_TRANSPORT")) && v[0]) {
        if (!strcmp(v, "ice")) cfg->transport = GENESIS_NET_TRANSPORT_ICE;
        else if (!strcmp(v, "lan")) cfg->transport = GENESIS_NET_TRANSPORT_LAN;
        else if (!strcmp(v, "hub")) cfg->transport = GENESIS_NET_TRANSPORT_HUB;
        else if (!strcmp(v, "relay")) cfg->transport = GENESIS_NET_TRANSPORT_RELAY;
    }
    if ((v = getenv("GENESIS_NET_SLOT_PORTS")) && v[0]) {
        /* "a,b,c,d": session slot i plays logical player slot_port[i]. */
        int i = 0;
        for (const char *p = v; *p && i < GENESIS_NETPLAY_MAX_SLOTS; i++) {
            cfg->slot_port[i] = (uint8_t)atoi(p);
            p = strchr(p, ',');
            if (!p) break;
            p++;
        }
        cfg->slot_port_valid = 1;
    }
}

/* ---- pad blob ---------------------------------------------------------------- */

static void encode_pad(uint16_t buttons, RNetInputSample *out, rnet_u32 tick)
{
    memset(out, 0, sizeof(*out));
    out->tick = tick;
    out->size = GENESIS_NETPLAY_PAD_BYTES;
    out->bytes[0] = (rnet_u8)(buttons & 0xFFu);
    out->bytes[1] = (rnet_u8)((buttons >> 8) & 0x0Fu);
    /* [2..3] sync bytes: no Genesis game needs them yet; kept zero. */
    out->valid = 1;
}

static uint16_t decode_pad(const RNetInputSample *in)
{
    if (!in || !in->valid || in->size < 2) return 0;
    return (uint16_t)((in->bytes[0] | ((uint16_t)in->bytes[1] << 8)) & GENESIS_NETPLAY_PAD_MASK);
}

static uint16_t test_pad(uint32_t tick)
{
    if (!g_np.test_period) return 0;
    return (tick % g_np.test_period) < g_np.test_hold ? g_np.test_mask : 0;
}

static void host_sample_local(rnet_u32 tick, RNetInputSample *out, void *ctx)
{
    GenesisNetplayState *st = (GenesisNetplayState *)ctx;
    uint16_t b = st->staged_valid ? st->staged_buttons : 0;
    if (st->test_period) b = test_pad(tick);
    encode_pad(b, out, tick);
}

static void apply_published(const uint16_t *rows, int slots)
{
    { static int dbg = -1; if (dbg < 0) dbg = getenv("GENESIS_NET_DEBUG_STAGE") != NULL;
      static uint16_t last[GENESIS_NETPLAY_MAX_SLOTS];
      for (int i = 0; dbg && rows && i < slots && i < GENESIS_NETPLAY_MAX_SLOTS; i++)
          if (rows[i] != last[i]) { fprintf(stderr, "[publish] sim=%u slot=%d rows=%03x\n",
                                            (unsigned)genesis_netplay_sim_tick(), i, rows[i]); last[i] = rows[i]; } }
    memset(g_np.published, 0, sizeof g_np.published);
    for (int i = 0; rows && i < slots && i < GENESIS_NETPLAY_MAX_SLOTS; i++)
        g_np.published[i] = rows[i] & GENESIS_NETPLAY_PAD_MASK;
}

/* Delay-sync publish (the rollback host publishes through np_rb_publish). */
static void host_publish(rnet_u32 tick, const RNetInputSample *by_slot, int slots, void *ctx)
{
    uint16_t rows[GENESIS_NETPLAY_MAX_SLOTS] = {0};
    (void)tick; (void)ctx;
    for (int i = 0; by_slot && i < slots && i < GENESIS_NETPLAY_MAX_SLOTS; i++)
        rows[i] = decode_pad(&by_slot[i]);
    apply_published(rows, slots);
}

static void np_rb_publish(uint32_t tick, const uint16_t *rows, int slots)
{
    (void)tick;
    apply_published(rows, slots);
}

/* ---- ICE signalling through the lobby WebSocket ------------------------------- */

#if defined(RNET_ENABLE_ICE)
static void host_on_signal(const RNetSignal *message, void *ctx)
{
    (void)ctx;
    if (message)
        (void)rnet_lobby_send_signal((int)message->type, (int)message->flag, message->text);
}
#endif

static void drain_lobby_signals(void)
{
#if defined(RNET_ENABLE_ICE)
    int type = 0, flag = 0;
    char text[2048];
    if (g_np.transport != GENESIS_NET_TRANSPORT_ICE) return;
    while (g_np.session && rnet_lobby_poll_signal(&type, &flag, text, sizeof(text))) {
        RNetSignal signal;
        memset(&signal, 0, sizeof(signal));
        if (type == (int)RNET_SIGNAL_LOCAL_SDP) type = (int)RNET_SIGNAL_REMOTE_SDP;
        else if (type == (int)RNET_SIGNAL_LOCAL_CANDIDATE) type = (int)RNET_SIGNAL_REMOTE_CANDIDATE;
        signal.type = (RNetSignalType)type;
        signal.flag = (rnet_u8)(flag & 0xff);
        snprintf(signal.text, sizeof signal.text, "%s", text);
        rnet_session_push_signal(g_np.session, &signal);
    }
#endif
}

/* ---- queries ------------------------------------------------------------------- */

int genesis_netplay_active(void) { return g_np.active && g_np.session; }
int genesis_netplay_rollback_active(void) { return genesis_netplay_active() && g_np.rollback; }
int genesis_netplay_is_running(void)
{
    return genesis_netplay_active() && rnet_session_is_running(g_np.session);
}
int genesis_netplay_is_spectator(void) { return genesis_netplay_active() && g_np.spectator; }
int genesis_netplay_local_slot(void) { return genesis_netplay_active() ? g_np.local_slot : -1; }
int genesis_netplay_slot_count(void) { return genesis_netplay_active() ? g_np.slot_count : 2; }
int genesis_netplay_input_player(void) { return genesis_netplay_active() ? g_np.input_player : 0; }
int genesis_netplay_is_host(void) { return genesis_netplay_active() && g_np.local_slot == 0; }
uint32_t genesis_netplay_session_id(void) { return g_np.cfg.session_id; }
uint32_t genesis_netplay_sim_tick(void)
{
    if (!genesis_netplay_active()) return 0;
    return g_np.rollback ? genesis_netplay_rb_sim_tick() : rnet_session_sim_tick(g_np.session);
}

const char *genesis_netplay_transport_name(void)
{
    if (!genesis_netplay_active()) return "none";
    switch (g_np.transport) {
    case GENESIS_NET_TRANSPORT_ICE:   return "ice";
    case GENESIS_NET_TRANSPORT_HUB:   return "lan-hub";
    case GENESIS_NET_TRANSPORT_RELAY: return "relay";
    default:                          return "lan";
    }
}

void genesis_netplay_request_return_to_lobby(void) { g_return_to_lobby = 1; }
int  genesis_netplay_return_to_lobby_requested(void) { return g_return_to_lobby; }
void genesis_netplay_clear_return_to_lobby(void) { g_return_to_lobby = 0; }
const char *genesis_netplay_refusal(void)
{
    return genesis_netplay_rollback_active() ? genesis_netplay_rb_refusal() : NULL;
}
int genesis_netplay_quiesced(void)
{
    return genesis_netplay_rollback_active() && genesis_netplay_rb_quiesced();
}

int genesis_netplay_draining(void)
{
    return genesis_netplay_rollback_active() && genesis_netplay_rb_draining();
}

void genesis_netplay_request_quiesce(void)
{
    if (genesis_netplay_rollback_active()) genesis_netplay_rb_request_quiesce();
}

void genesis_netplay_set_tick_runner(GenesisNetplayRunTick fn) { g_tick_runner = fn; }

void genesis_netplay_sim_input(GenesisSimInput *out)
{
    memset(out, 0, sizeof *out);
    out->pad_type[0] = g_np.pad_type[0];
    out->pad_type[1] = g_np.pad_type[1];
    for (int s = 0; s < g_np.slot_count && s < GENESIS_NETPLAY_MAX_SLOTS; s++) {
        int p = g_np.slot_port[s];
        if (p < 0 || p >= GENESIS_SIM_MAX_PLAYERS) continue;
        if (g_np.occupied_mask && !(g_np.occupied_mask & (1u << s))) continue;
        out->pad[p] = g_np.published[s];
        out->human_mask |= 1u << p;
    }
}

uint16_t genesis_netplay_published_pad(int player)
{
    GenesisSimInput in;
    if (player < 0 || player >= GENESIS_SIM_MAX_PLAYERS) return 0;
    genesis_netplay_sim_input(&in);
    return in.pad[player];
}

/* ---- rollback host binding ------------------------------------------------------ */

static void np_rb_run_tick(void)
{
    GenesisSimInput in;
    genesis_netplay_sim_input(&in);
    if (g_tick_runner) g_tick_runner(&in);
}

static void np_rollback_try_start(void)
{
    GenesisNetplayRbBindings b;
    g_np.rollback = 0;
    if (!g_np.cfg.rollback) return;
    memset(&b, 0, sizeof b);
    b.session = &g_np.session;
    b.local_slot = &g_np.local_slot;
    b.slot_count = &g_np.slot_count;
    b.input_delay = &g_np.input_delay;
    b.input_prediction = &g_np.input_prediction;
    b.occupied_mask = g_np.occupied_mask;
    b.publish = np_rb_publish;
    b.run_tick = np_rb_run_tick;
    genesis_netplay_rb_bind(&b);
    if (genesis_netplay_rb_start()) {
        g_np.rollback = 1;
    } else {
        /* Doctrine: abort rather than silently degrade. A peer that fell back
         * to delay-sync while the others run rollback would never agree on an
         * admission protocol; refuse the match instead. */
        fprintf(stderr, "genesis_netplay: rollback host failed to start — refusing the match\n");
        genesis_netplay_rb_bind(NULL);
        g_return_to_lobby = 1;
    }
}

/* ---- start / stop ---------------------------------------------------------------- */

static int resolve_transport(const GenesisNetplayConfig *cfg, int seats)
{
    if (cfg->transport == GENESIS_NET_TRANSPORT_ICE) {
#if defined(RNET_ENABLE_ICE)
        if (seats != 2 || cfg->spectator) {
            fprintf(stderr, "genesis_netplay: ICE carries exactly two players; "
                            "%d seats%s need the relay\n", seats,
                    cfg->spectator ? " and a spectator" : "");
            return -1;
        }
        return GENESIS_NET_TRANSPORT_ICE;
#else
        fprintf(stderr, "genesis_netplay: ICE requested but this build has no ICE\n");
        return -1;
#endif
    }
    if (cfg->transport == GENESIS_NET_TRANSPORT_RELAY) return GENESIS_NET_TRANSPORT_RELAY;
    if (cfg->transport == GENESIS_NET_TRANSPORT_HUB) return GENESIS_NET_TRANSPORT_HUB;
    if (cfg->transport == GENESIS_NET_TRANSPORT_LAN) {
        if (seats > 2) return GENESIS_NET_TRANSPORT_HUB;
        return GENESIS_NET_TRANSPORT_LAN;
    }
    /* auto */
    return seats > 2 ? GENESIS_NET_TRANSPORT_HUB : GENESIS_NET_TRANSPORT_LAN;
}

#if defined(RNET_ENABLE_ICE)
static char s_ice_stun[128], s_ice_turn[128], s_ice_user[192], s_ice_pass[128];
static int start_ice(const GenesisNetplayConfig *cfg)
{
    RNetIceConfig ice;
    rnet_ice_config_init_defaults(&ice);
    ice.controlling = g_np.local_slot == 0 ? 1u : 0u;
    if (rnet_lobby_connected()) {
        (void)rnet_lobby_request_turn_credentials();
        for (int i = 0; i < 50; i++) {
            const RNetLobbyTurnCredentials *tc = rnet_lobby_turn_credentials();
            if (tc && tc->valid) break;
            rnet_lobby_pump();
            np_sleep_ms(10);
        }
        const RNetLobbyTurnCredentials *tc = rnet_lobby_turn_credentials();
        if (tc && tc->valid) {
            if (tc->stun_host[0]) {
                snprintf(s_ice_stun, sizeof s_ice_stun, "%s", tc->stun_host);
                ice.stun_host = s_ice_stun;
                ice.stun_port = (rnet_u16)(tc->stun_port > 0 ? tc->stun_port : 3478);
            }
            snprintf(s_ice_turn, sizeof s_ice_turn, "%s", tc->turn_host);
            snprintf(s_ice_user, sizeof s_ice_user, "%s", tc->username);
            snprintf(s_ice_pass, sizeof s_ice_pass, "%s", tc->password);
            ice.turn_host = s_ice_turn; ice.turn_user = s_ice_user; ice.turn_pass = s_ice_pass;
            ice.turn_port = (rnet_u16)(tc->turn_port > 0 ? tc->turn_port : 3478);
        }
    }
    if (cfg->force_turn) {
        if (!ice.turn_host) {
            fprintf(stderr, "genesis_netplay: FORCE_TURN without TURN credentials — refusing\n");
            return -1;
        }
        ice.force_relay = 1;
    }
    return rnet_session_start_ice(g_np.session, &ice);
}
#endif

int genesis_netplay_start(const GenesisNetplayConfig *cfg)
{
    RNetConfig rcfg;
    RNetHostVTable host;
    int seats, transport, rc = 0;

    if (!cfg || !cfg->enabled) return -1;
    genesis_netplay_shutdown();
    g_return_to_lobby = 0;
    g_np.cfg = *cfg;

    seats = cfg->slot_count > 0 ? cfg->slot_count : 2;
    if (seats > GENESIS_NETPLAY_MAX_SLOTS) {
        fprintf(stderr, "genesis_netplay: %d seats requested; Genesis carries at most %d "
                        "logical players — refusing\n", seats, GENESIS_NETPLAY_MAX_SLOTS);
        return -1;
    }
    transport = resolve_transport(cfg, seats);
    if (transport < 0) return -4;
    if (cfg->spectator && transport != GENESIS_NET_TRANSPORT_RELAY &&
        transport != GENESIS_NET_TRANSPORT_HUB) {
        fprintf(stderr, "genesis_netplay: a spectator needs a relay (the lobby's input "
                        "relay or a LAN hub) — refusing rather than showing a black screen\n");
        return -5;
    }

    rnet_config_init_defaults(&rcfg);
    rcfg.slot_count = (rnet_u8)(seats < 2 ? 2 : seats);
    if (cfg->spectator) {
        if (cfg->spectator_wire_slot < seats || cfg->spectator_wire_slot > 0xff) {
            fprintf(stderr, "genesis_netplay: spectator without a usable relay slot (%d, "
                            "seats=%d) — refusing\n", cfg->spectator_wire_slot, seats);
            return -1;
        }
        rcfg.local_slot = (rnet_u8)rcfg.slot_count;   /* observer sentinel */
        rcfg.wire_slot = (rnet_u8)cfg->spectator_wire_slot;
    } else {
        int slot = cfg->local_slot < 0 ? 0 : cfg->local_slot;
        if (slot >= rcfg.slot_count) slot = rcfg.slot_count - 1;
        rcfg.local_slot = (rnet_u8)slot;
    }
    rcfg.input_delay = (rnet_u8)(cfg->input_delay < 0 ? 0 : cfg->input_delay > 20 ? 20 : cfg->input_delay);
    rcfg.session_id = cfg->session_id ? cfg->session_id : 1u;
    rcfg.occupied_mask = cfg->occupied_mask;

    memset(&host, 0, sizeof host);
    host.sample_local = host_sample_local;
    host.publish = host_publish;
    host.ctx = &g_np;
#if defined(RNET_ENABLE_ICE)
    if (transport == GENESIS_NET_TRANSPORT_ICE) host.on_signal = host_on_signal;
#endif
    g_np.session = rnet_session_create(&rcfg, &host);
    if (!g_np.session) return -2;
    g_np.local_slot = (int)rcfg.local_slot;
    g_np.transport = transport;

    switch (transport) {
    case GENESIS_NET_TRANSPORT_ICE:
#if defined(RNET_ENABLE_ICE)
        rc = start_ice(cfg);
#else
        rc = -1;
#endif
        break;
    case GENESIS_NET_TRANSPORT_HUB:
        rc = (!cfg->spectator && rcfg.local_slot == 0)
                 ? rnet_session_start_lan_hub(g_np.session, cfg->bind_hostport)
                 : rnet_session_start_lan(g_np.session, cfg->bind_hostport, cfg->peer_hostport);
        break;
    default:   /* LAN and the lobby relay are the same UDP transport */
        rc = rnet_session_start_lan(g_np.session, cfg->bind_hostport, cfg->peer_hostport);
        break;
    }
    if (rc != 0) {
        fprintf(stderr, "genesis_netplay: transport %s failed to start\n",
                genesis_netplay_transport_name());
        rnet_session_destroy(g_np.session);
        memset(&g_np, 0, sizeof g_np);
        return -3;
    }

    g_np.active = 1;
    g_np.slot_count = (int)rcfg.slot_count;
    g_np.spectator = cfg->spectator ? 1 : 0;
    g_np.input_player = cfg->input_player >= 0 && cfg->input_player < GENESIS_SIM_MAX_PLAYERS
                            ? cfg->input_player : 0;
    g_np.input_delay = (int)rcfg.input_delay;
    g_np.input_prediction = cfg->input_prediction;
    if (g_np.input_prediction && g_np.input_prediction < 2) g_np.input_prediction = 2;
    if (g_np.input_prediction > 32) g_np.input_prediction = 32;
    g_np.occupied_mask = cfg->occupied_mask;
    for (int i = 0; i < GENESIS_NETPLAY_MAX_SLOTS; i++)
        g_np.slot_port[i] = cfg->slot_port_valid || cfg->slot_port[i] ? cfg->slot_port[i] : (uint8_t)i;
    {
        const char *tp = getenv("GENESIS_NET_TEST_PAD");
        if (tp && tp[0] && !g_np.spectator) {
            unsigned a = 0, b = 0, m = 0;
            int n = sscanf(tp, "%u:%u:%x", &a, &b, &m);
            g_np.test_period = a;
            g_np.test_hold = n >= 2 ? b : (a > 1 ? a / 2 : 1);
            g_np.test_mask = (uint16_t)(n >= 3 ? m : 0x0008u /* RIGHT */);
            fprintf(stderr, "genesis_netplay: TEST PAD period=%u hold=%u mask=%03x "
                            "(validation only)\n", g_np.test_period, g_np.test_hold, g_np.test_mask);
        }
    }

    np_rollback_try_start();

    fprintf(stderr, "genesis_netplay: started transport=%s slot=%d slots=%d input=%d "
                    "session=%u delay=%u prediction=%d mode=%s occupied=%x ports=%u,%u,%u,%u "
                    "bind=%s peer=%s%s\n",
            genesis_netplay_transport_name(), g_np.local_slot, g_np.slot_count,
            g_np.input_player, (unsigned)rcfg.session_id, (unsigned)rcfg.input_delay,
            g_np.input_prediction, g_np.rollback ? "rollback" : "delay",
            (unsigned)g_np.occupied_mask, g_np.slot_port[0], g_np.slot_port[1],
            g_np.slot_port[2], g_np.slot_port[3], cfg->bind_hostport, cfg->peer_hostport,
            g_np.spectator ? " SPECTATOR" : "");
    return 0;
}

void genesis_netplay_print_summary(void)
{
    if (!g_np.active) return;
    if (g_np.rollback) genesis_netplay_rb_print_summary();
    fprintf(stderr, "genesis_netplay: summary frames=%u sim=%u transport=%s\n",
            (unsigned)g_np.frames_finished, (unsigned)genesis_netplay_sim_tick(),
            genesis_netplay_transport_name());
}

void genesis_netplay_shutdown(void)
{
    if (g_np.rollback) {
        genesis_netplay_rb_shutdown();
        genesis_netplay_rb_bind(NULL);
    }
    if (g_np.session) {
        (void)rnet_session_send_bye(g_np.session);
        rnet_session_destroy(g_np.session);
    }
    {
        uint8_t keep[2] = { g_np.pad_type[0], g_np.pad_type[1] };
        memset(&g_np, 0, sizeof(g_np));
        g_np.pad_type[0] = keep[0]; g_np.pad_type[1] = keep[1];
    }
}

/* ---- per tick ------------------------------------------------------------------- */

int genesis_netplay_needs_local_sample(void)
{
    uint32_t tick;
    if (!genesis_netplay_active() || g_np.spectator) return 0;
    if (g_np.rollback) return 1;   /* re-read on every admit attempt */
    if (!rnet_session_is_running(g_np.session)) return 1;
    tick = rnet_session_sim_tick(g_np.session);
    return !g_np.latched_for_tick || g_np.latched_sim_tick != tick;
}

void genesis_netplay_stage_local(uint16_t buttons)
{
    uint32_t tick;
    if (!genesis_netplay_active()) return;
    buttons &= GENESIS_NETPLAY_PAD_MASK;
    { static uint16_t last; if (getenv("GENESIS_NET_DEBUG_STAGE") && buttons != last) {
        fprintf(stderr, "[stage] sim=%u buttons=%03x\n", (unsigned)genesis_netplay_sim_tick(), buttons); last = buttons; } }
    if (g_np.rollback) {
        g_np.staged_buttons = buttons;
        g_np.staged_valid = 1;
        return;
    }
    tick = rnet_session_sim_tick(g_np.session);
    if (g_np.latched_for_tick && g_np.latched_sim_tick == tick) return;
    g_np.staged_buttons = buttons;
    g_np.staged_valid = 1;
    g_np.latched_for_tick = 1;
    g_np.latched_sim_tick = tick;
}

/* The initial SRAM barrier: the host sends, every guest applies; nobody
 * admits tick 0 before it completes, so the boot digest covers the host's
 * save on every peer. */
static int sram_barrier(void)
{
    rnet_u8 op = 0, slot = 0;
    const void *data = NULL;
    size_t size = 0;
    if (g_np.sram_sync_done) return 1;
    if (!g_sram || !g_sram_size) { g_np.sram_sync_done = 1; return 1; }
    if (!rnet_session_is_running(g_np.session)) return 0;
    if (g_np.local_slot == 0 && !g_np.spectator && !g_np.sram_sync_sent) {
        if (rnet_session_state_begin(g_np.session, RNET_STATE_OP_SRAM, 0, g_sram, g_sram_size) != 0) {
            fprintf(stderr, "genesis_netplay: SRAM sync could not start — refusing the match\n");
            g_return_to_lobby = 1;
            return 0;
        }
        g_np.sram_sync_sent = 1;
        fprintf(stderr, "genesis_netplay: syncing host SRAM (%u bytes)\n", (unsigned)g_sram_size);
    }
    if (rnet_session_state_take_ready(g_np.session, &op, &slot, &data, &size)) {
        if (op == RNET_STATE_OP_SRAM && g_np.local_slot != 0 && data && size) {
            size_t n = size < g_sram_size ? size : g_sram_size;
            memcpy(g_sram, data, n);
            if (n < g_sram_size) memset(g_sram + n, 0, g_sram_size - n);
            fprintf(stderr, "genesis_netplay: applied host SRAM (%zu bytes)\n", size);
        }
        rnet_session_state_finish(g_np.session, 0);
        if (op == RNET_STATE_OP_SRAM) {
            g_np.sram_sync_done = 1;
            if (g_np.local_slot == 0) fprintf(stderr, "genesis_netplay: host SRAM delivered\n");
        }
    }
    return g_np.sram_sync_done;
}

int genesis_netplay_poll_admit(void)
{
    uint32_t tick;
    if (!genesis_netplay_active()) return 1;
    if (g_np.transport == GENESIS_NET_TRANSPORT_ICE || rnet_lobby_connected())
        rnet_lobby_pump();
    drain_lobby_signals();
    rnet_session_pump(g_np.session);
    if (!sram_barrier()) return 0;
    if (g_np.rollback) {
        /* Idempotent between an admission and its finish_frame: the host
         * loop polls again around present to overlap wire work, and the
         * driver must see exactly one poll per admitted tick. */
        if (g_np.needs_advance) return 1;
        int admitted = genesis_netplay_rb_poll_admit();
        if (genesis_netplay_rb_refusal()) g_return_to_lobby = 1;
        if (admitted) g_np.needs_advance = 1;
        return admitted;
    }
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
    if (!genesis_netplay_active()) return;
    if (g_np.rollback) {
        if (!g_np.needs_advance) return;
        genesis_netplay_rb_finish_frame();
        g_np.needs_advance = 0;
        g_np.frames_finished++;
        return;
    }
    g_np.frames_finished++;
    if (!g_np.needs_advance) return;
    rnet_session_advance(g_np.session);
    g_np.needs_advance = 0;
    g_np.latched_for_tick = 0;
}

int genesis_netplay_input_desync(uint32_t *tick, uint32_t *local_hash, uint32_t *remote_hash)
{
    return genesis_netplay_active() && !g_np.rollback &&
           rnet_session_input_desync(g_np.session, tick, local_hash, remote_hash);
}

int genesis_netplay_peer_disconnected(uint32_t timeout_ms)
{
    if (!genesis_netplay_active()) return 0;
    return rnet_session_peer_disconnected(g_np.session, timeout_ms ? timeout_ms : 1500);
}

/* ---- config seal + identity -------------------------------------------------------- */

static char s_config_image[512];
static GenesisSessionConfig s_local_cfg, s_session_cfg;
static int s_session_cfg_adopted;
static char s_engine_knobs[256];
void genesis_netplay_set_engine_knobs(const char *line)
{
    snprintf(s_engine_knobs, sizeof s_engine_knobs, "%s", line ? line : "");
}

void genesis_netplay_set_local_session_config(const GenesisSessionConfig *c)
{
    if (c) s_local_cfg = *c;
    if (!s_session_cfg_adopted) s_session_cfg = s_local_cfg;
}

void genesis_netplay_adopt_session_config(const GenesisSessionConfig *host)
{
    s_session_cfg_adopted = host != NULL;
    s_session_cfg = host ? *host : s_local_cfg;
    s_session_cfg.game[sizeof s_session_cfg.game - 1] = 0;
    s_session_cfg.video[sizeof s_session_cfg.video - 1] = 0;
}

const GenesisSessionConfig *genesis_netplay_session_config(void) { return &s_session_cfg; }

void genesis_netplay_config_seal(void)
{
    const GenesisSessionConfig *c = &s_session_cfg;
    g_np.pad_type[0] = c->pad_type[0] ? 1 : 0;
    g_np.pad_type[1] = c->pad_type[1] ? 1 : 0;
    snprintf(s_config_image, sizeof s_config_image,
             "genesis-config/1\npad=%u,%u\nws=%u cells=%u\nvideo=%s\ngame=%s\nknobs=%s\n",
             (unsigned)g_np.pad_type[0], (unsigned)g_np.pad_type[1],
             (unsigned)(c->ws_on ? 1 : 0), (unsigned)(c->ws_on ? c->ws_cells : 0),
             c->video[0] ? c->video : "off", c->game, s_engine_knobs);
    genesis_netplay_rb_set_config_image(s_config_image);
    fprintf(stderr, "genesis_netplay: session config (%s):\n%s",
            s_session_cfg_adopted ? "adopted from the host" : "local", s_config_image);
}

const char *genesis_netplay_config_image(void) { return s_config_image; }

void genesis_netplay_set_identity(uint32_t build_fp, const uint8_t rom_sha256[32])
{
    /* content = FNV-1a over the ROM digest and the sealed config image: two
     * peers with the same ROM but a different session configuration are
     * running different games and must be refused (NETPLAY.md section 4). */
    uint32_t h = 2166136261u;
    for (int i = 0; rom_sha256 && i < 32; i++) { h ^= rom_sha256[i]; h *= 16777619u; }
    for (const char *p = s_config_image; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    genesis_netplay_rb_set_identity(build_fp, h ? h : 1u);
}

/*
 * genesis_netplay_rb.c -- RNetRbHost for the Genesis runner (see .h).
 *
 * Shape copied from snesrecomp runner/src/netplay/snes_netplay_rb.c; the long
 * comments on WHY the driver does what it does live with the driver in
 * recomp-net (docs/rollback.md).
 *
 * REPLAY SHAPE: INLINE. The driver loads the baseline and runs the whole
 * replay inside one poll_admit through run_tick. Justification, measured by
 * the determinism probe (GENESIS_RB_PROBE, 691 passes x 12 ticks, Linux gcc
 * Release): a replayed Genesis tick costs p50 0.68-0.92 ms / p99 0.87-1.01 ms
 * (S2, S1, S3K), a snapshot save 0.011 ms and a load 0.009 ms p50. A replay
 * bounded by the prediction cap (P <= 16) therefore costs < ~17 ms of CPU,
 * inside one host iteration, and genesis_sim_step returns (the game fiber is
 * suspended between ticks) -- the SNES case, not the N64 one. INCREMENTAL
 * would spread a replay over host iterations and hold presentation for as
 * many frames; nothing here needs that. Both shapes are behaviourally
 * identical on the wire (rb_driver.h).
 *
 * PRESENTATION DURING A REPLAY (NETPLAY.md section 1): the bound runner runs
 * genesis_sim_step with no scanline sink (the VDP still renders for its
 * status flags, nothing is presented) and drains audio into sim_step's
 * scratch buffers: replayed ticks re-clock the YM/PSG models, which is
 * simulation, but what they produce is never queued to the device, so the
 * device stream is never rewound. resim_begin/end only bracket that.
 */
#include "genesis_netplay_rb.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_net/rb_driver.h"
#include "retcomm_rbengine/snap_ring.h"
#include "retcomm_rbengine/mono_ms.h"
#include "rb_state.h"
#include "sim_step.h"

#define RB_MAX_SLOTS   RNET_RB_MAX_SLOTS
#define RB_PAD_MASK    0x0FFFu
#define RB_PSX_NEUTRAL 0xFFFFu

static struct {
    GenesisNetplayRbBindings b;
    RNetRbDriver *drv;
    RbeSnapRing  *snaps;
    uint32_t      snap_depth;
    uint16_t      rows[RB_MAX_SLOTS];
    int           in_resim;
    const char   *config_image;
    uint32_t      replayed_ticks;
} g_rb;

static int rb_env_int(const char *name, const char *generic, int def, int lo, int hi)
{
    const char *v = getenv(name);
    long n;
    char *end;
    if (!v && generic) v = getenv(generic);
    if (!v || !v[0]) return def;
    n = strtol(v, &end, 10);
    if (end == v || *end != '\0' || n < lo || n > hi) return def;
    return (int)n;
}

static RNetRbDriver *rb_drv(void)
{
    if (!g_rb.drv) g_rb.drv = rnet_rb_driver_create();
    return g_rb.drv;
}

/* ---- snapshots: rb_state blobs in rbengine's tick ring ---------------------------- */

static int rb_snap_serialize(void *ctx, uint32_t tick, uint8_t **out, size_t *len)
{
    (void)ctx; (void)tick;
    size_t need = genesis_rb_bound();
    uint8_t *blob = (uint8_t *)malloc(need);
    if (!blob) return 0;
    size_t n = genesis_rb_save(blob, need);
    if (!n) { free(blob); return 0; }
    *out = blob;
    *len = n;
    return 1;
}

static int rb_snap_deserialize(void *ctx, uint32_t tick, const uint8_t *data, size_t len)
{
    (void)ctx; (void)tick;
    return genesis_rb_load(data, len);
}

static const RbeSnapVTable g_snap_vt = { NULL, rb_snap_serialize, rb_snap_deserialize };

static int  h_snap_save(void *c, uint32_t t) { (void)c; return g_rb.snaps ? rbe_snap_ring_save(g_rb.snaps, t, &g_snap_vt) : 0; }
static int  h_snap_load(void *c, uint32_t t) { (void)c; return g_rb.snaps ? rbe_snap_ring_load(g_rb.snaps, t, &g_snap_vt) : 0; }
static int  h_snap_has(void *c, uint32_t t)  { (void)c; return g_rb.snaps ? rbe_snap_ring_has(g_rb.snaps, t) : 0; }
static int  h_snap_oldest(void *c, uint32_t *o)
{
    (void)c;
    if (!g_rb.snaps || rbe_snap_ring_count(g_rb.snaps) == 0) return 0;
    *o = rbe_snap_ring_oldest_tick(g_rb.snaps);
    return 1;
}
static void h_snap_drop_after(void *c, uint32_t t) { (void)c; if (g_rb.snaps) (void)rbe_snap_ring_drop_after(g_rb.snaps, t); }

/* ---- one tick --------------------------------------------------------------------- */

static void h_publish(void *ctx, uint32_t tick, const RNetRbFrame *rows, int slots, int replay)
{
    (void)ctx; (void)replay;
    for (int i = 0; i < RB_MAX_SLOTS; i++) g_rb.rows[i] = 0;
    for (int i = 0; i < slots && i < RB_MAX_SLOTS; i++)
        g_rb.rows[i] = (uint16_t)(rows[i].buttons & RB_PAD_MASK);
    if (g_rb.b.publish) g_rb.b.publish(tick, g_rb.rows, slots);
}

static int h_run_tick(void *ctx, uint32_t tick)
{
    (void)ctx; (void)tick;
    if (!g_rb.b.run_tick) return 0;
    g_rb.b.run_tick();
    g_rb.replayed_ticks++;
    return 1;
}

static void h_resim_begin(void *ctx) { (void)ctx; g_rb.in_resim = 1; }
static void h_resim_end(void *ctx)   { (void)ctx; g_rb.in_resim = 0; }

/* ---- digests ---------------------------------------------------------------------- */

static uint32_t h_digest_master(void *ctx)
{
    GenesisRbDigest d;
    (void)ctx;
    genesis_rb_digest(&d);
    return genesis_rb_fold32(d.master);
}

/* The three wire partitions: work RAM, the sound side (YM/PSG models, the
 * event queue, the Z80 and the bus it shares), and the VDP. Everything else
 * (cpu, sched, exec, input, game) is in the master only; the boot line and
 * fork logs print all twelve locally. */
static void h_digest_parts(void *ctx, RNetRbDigestParts *out)
{
    GenesisRbDigest d;
    uint64_t audio = 0xcbf29ce484222325ull;
    (void)ctx;
    genesis_rb_digest(&d);
    const int sound[] = { GENESIS_RB_PART_FM, GENESIS_RB_PART_PSG, GENESIS_RB_PART_EVQ,
                          GENESIS_RB_PART_Z80, GENESIS_RB_PART_BUS };
    for (unsigned i = 0; i < sizeof sound / sizeof sound[0]; i++) {
        audio ^= d.part[sound[i]];
        audio *= 0x100000001b3ull;
    }
    out->master = genesis_rb_fold32(d.master);
    out->part[0] = genesis_rb_fold32(d.part[GENESIS_RB_PART_RAM]);
    out->part[1] = genesis_rb_fold32(audio);
    out->part[2] = genesis_rb_fold32(d.part[GENESIS_RB_PART_VDP]);
}

/* ---- pads ------------------------------------------------------------------------- */

static void h_decode(void *ctx, int slot, const RNetInputSample *in, RNetRbFrame *out)
{
    (void)ctx; (void)slot;
    out->buttons = (uint16_t)((in->bytes[0] | ((uint16_t)in->bytes[1] << 8)) & RB_PAD_MASK);
    out->stick_x = 0;
    out->stick_y = 0;
    out->analog = 0;
}

static void h_sanitize(void *ctx, int slot, RNetRbFrame *f)
{
    (void)ctx; (void)slot;
    if (f->buttons == RB_PSX_NEUTRAL) f->buttons = 0;   /* foreign neutral -> ours */
    f->buttons &= RB_PAD_MASK;
    f->stick_x = 0;
    f->stick_y = 0;
    f->analog = 0;
}

static void h_neutral(void *ctx, int slot, RNetRbFrame *out)
{
    (void)ctx; (void)slot;
    out->buttons = 0;   /* active high: nothing pressed */
}

/* ---- session control ---------------------------------------------------------------- */

static void h_boot_digest_noted(void *ctx)
{
    GenesisRbDigest d;
    (void)ctx;
    genesis_rb_digest(&d);
    fprintf(stderr, "genesis_netplay: RB boot parts master=%08x", genesis_rb_fold32(d.master));
    for (int p = 0; p < GENESIS_RB_PART_COUNT; p++)
        fprintf(stderr, " %s=%08x", genesis_rb_part_name(p), genesis_rb_fold32(d.part[p]));
    fprintf(stderr, "\ngenesis_netplay: RB boot taken at sim_tick=%u\n", genesis_sim_tick_count());
}

extern void genesis_netplay_request_return_to_lobby(void);
static void h_return_to_lobby(void *ctx) { (void)ctx; genesis_netplay_request_return_to_lobby(); }
static uint32_t h_now_ms(void *ctx) { (void)ctx; return rbe_mono_ms(); }

/* ---- config seal via the mod-set handshake ----------------------------------------- */

static int modset_check(const char *want, char *reason, uint32_t cap)
{
    if (g_rb.config_image && want && !strcmp(want, g_rb.config_image)) return 0;
    snprintf(reason, cap, "session configuration differs from the host's");
    return 1;
}

void genesis_netplay_rb_set_config_image(const char *text)
{
    g_rb.config_image = text;
    rnet_rb_driver_set_modset(rb_drv(), text, modset_check, NULL);
}

void genesis_netplay_rb_set_identity(uint32_t build_fp, uint32_t content_fp)
{
    rnet_rb_driver_set_identity(rb_drv(), build_fp, content_fp);
    fprintf(stderr, "genesis_netplay: identity build=%08x content=%08x\n",
            (unsigned)build_fp, (unsigned)content_fp);
}

/* ---- coordinated stop (SIGUSR1) ------------------------------------------------------ */

static volatile sig_atomic_t s_quiesce_signalled;
#if !defined(_WIN32)
static void rb_on_sigusr1(int sig) { (void)sig; s_quiesce_signalled = 1; }
#endif
static void rb_install_quiesce_signal(void)
{
#if !defined(_WIN32)
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = rb_on_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);
#endif
}

int genesis_netplay_rb_draining(void)
{
    return g_rb.drv && rnet_rb_driver_quiesce_state(g_rb.drv) == RNET_RB_QUIESCE_DRAINING;
}

void genesis_netplay_rb_request_quiesce(void)
{
    if (g_rb.drv && rnet_rb_driver_quiesce_state(g_rb.drv) == RNET_RB_QUIESCE_NONE) {
        fprintf(stderr, "genesis_netplay: quiesce requested — draining rollback\n");
        rnet_rb_driver_request_quiesce(g_rb.drv);
    }
}

int genesis_netplay_rb_quiesced(void)
{
    if (!g_rb.drv) return 0;
    RNetRbQuiesce q = rnet_rb_driver_quiesce_state(g_rb.drv);
    return q == RNET_RB_QUIESCE_DRAINED || q == RNET_RB_QUIESCE_TIMED_OUT;
}

/* ---- lifecycle ------------------------------------------------------------------------- */

void genesis_netplay_rb_bind(const GenesisNetplayRbBindings *b)
{
    if (!b) { memset(&g_rb.b, 0, sizeof g_rb.b); return; }
    g_rb.b = *b;
}

int genesis_netplay_rb_start(void)
{
    RNetRbDriverConfig cfg;
    RNetRbHost host;
    RNetRbDriver *drv = rb_drv();

    genesis_netplay_rb_shutdown();
    {
        GenesisNetplayRbBindings keep = g_rb.b;
        const char *img = g_rb.config_image;
        memset(&g_rb, 0, sizeof g_rb);
        g_rb.b = keep;
        g_rb.drv = drv;
        g_rb.config_image = img;
    }
    if (!drv || !g_rb.b.session || !g_rb.b.run_tick) {
        fprintf(stderr, "genesis_netplay: RB start refused — %s\n",
                !drv ? "no driver" : "bindings missing (bind before start)");
        return 0;
    }
    /* Depth floor 16 (snesrecomp measured 8 losing episodes to NACKs at
     * 200 ms). Every tick is snapshotted (interval 1), so depth is ticks. */
    g_rb.snap_depth = (uint32_t)rb_env_int("GENESIS_RB_SNAP_DEPTH", "RNET_RB_SNAP_DEPTH",
                                           (int)RBE_SNAP_RING_DEFAULT_DEPTH, 16, 240);
    g_rb.snaps = rbe_snap_ring_create(g_rb.snap_depth);
    if (!g_rb.snaps) return 0;

    memset(&cfg, 0, sizeof cfg);
    cfg.session = g_rb.b.session;
    cfg.local_slot = g_rb.b.local_slot;
    cfg.slot_count = g_rb.b.slot_count;
    cfg.input_delay = g_rb.b.input_delay;
    cfg.input_prediction = g_rb.b.input_prediction;
    cfg.occupied_mask = g_rb.b.occupied_mask;
    cfg.replay_mode = RNET_RB_REPLAY_INLINE;
    cfg.part_names[0] = "ram";
    cfg.part_names[1] = "sound";
    cfg.part_names[2] = "vdp";
    cfg.snap_depth = g_rb.snap_depth;
    cfg.log_prefix = "genesis_netplay";
    cfg.env_alias = "GENESIS_RB";
    /* FORCE_MISPREDICT must flip bits the guest reads: START (title, menus)
     * and B (jump in gameplay). The driver's historical 0x0040 is GPAD_A,
     * also read, but START is what an attract/title scene samples. */
    cfg.inject_flip_bits = 0x0090u;

    memset(&host, 0, sizeof host);
    host.snap_save = h_snap_save;
    host.snap_load = h_snap_load;
    host.snap_has = h_snap_has;
    host.snap_oldest = h_snap_oldest;
    host.snap_drop_after = h_snap_drop_after;
    host.publish = h_publish;
    host.run_tick = h_run_tick;
    host.resim_begin = h_resim_begin;
    host.resim_end = h_resim_end;
    host.digest_master = h_digest_master;
    host.digest_parts = h_digest_parts;
    host.decode_sample = h_decode;
    host.sanitize_row = h_sanitize;
    host.neutral_row = h_neutral;
    host.boot_digest_noted = h_boot_digest_noted;
    host.request_return_to_lobby = h_return_to_lobby;
    host.now_ms = h_now_ms;

    if (!rnet_rb_driver_start(drv, &cfg, &host)) {
        rbe_snap_ring_destroy(g_rb.snaps);
        g_rb.snaps = NULL;
        return 0;
    }
    s_quiesce_signalled = 0;
    rb_install_quiesce_signal();
    return 1;
}

void genesis_netplay_rb_shutdown(void)
{
    if (g_rb.drv) rnet_rb_driver_shutdown(g_rb.drv);
    if (g_rb.snaps) { rbe_snap_ring_destroy(g_rb.snaps); g_rb.snaps = NULL; }
    g_rb.in_resim = 0;
}

int genesis_netplay_rb_poll_admit(void)
{
    if (s_quiesce_signalled && g_rb.drv &&
        rnet_rb_driver_quiesce_state(g_rb.drv) == RNET_RB_QUIESCE_NONE) {
        fprintf(stderr, "genesis_netplay: SIGUSR1 — draining rollback, then exiting\n");
        rnet_rb_driver_request_quiesce(g_rb.drv);
    }
    /* INLINE: a replay runs inside this call; only LIVE comes back. */
    int live = g_rb.drv && rnet_rb_driver_poll_admit(g_rb.drv) == RNET_RB_ADMIT_LIVE;
    /* A stall that lasts is said once per 2 s, with the driver's tag. */
    static uint32_t since, said;
    if (live) {
        since = 0;
    } else if (g_rb.drv) {
        uint32_t now = rbe_mono_ms();
        if (!since) since = now;
        if (now - since >= 2000u && now - said >= 2000u) {
            const char *tag = rnet_rb_driver_stall_tag(g_rb.drv);
            said = now;
            fprintf(stderr, "genesis_netplay: stalled %u ms at sim=%u (%s)\n",
                    (unsigned)(now - since), (unsigned)rnet_rb_driver_sim_tick(g_rb.drv),
                    tag ? tag : "?");
        }
    }
    return live;
}

void genesis_netplay_rb_finish_frame(void) { if (g_rb.drv) rnet_rb_driver_finish_frame(g_rb.drv); }
int  genesis_netplay_rb_in_resim(void) { return g_rb.in_resim; }
uint32_t genesis_netplay_rb_sim_tick(void) { return g_rb.drv ? rnet_rb_driver_sim_tick(g_rb.drv) : 0; }
const char *genesis_netplay_rb_refusal(void) { return g_rb.drv ? rnet_rb_driver_refusal(g_rb.drv) : NULL; }

int genesis_netplay_rb_last_fork(uint32_t *tick, const char **partition,
                                 uint32_t *mine, uint32_t *theirs)
{
    if (!g_rb.drv || !rnet_rb_driver_last_fork(g_rb.drv, tick, partition)) return 0;
    rnet_rb_driver_fork_digests(g_rb.drv, mine, theirs);
    return 1;
}

void genesis_netplay_rb_print_summary(void)
{
    if (!g_rb.drv) return;
    fprintf(stderr, "NETPLAY_DRIVER sim=%u episodes=%u invents=%u promotes=%u resim_ticks=%llu "
                    "replayed=%u desyncs=%u rtt_ms=%u confirmed=%u refusal=%s\n",
            (unsigned)rnet_rb_driver_sim_tick(g_rb.drv),
            (unsigned)rnet_rb_driver_episode_count(g_rb.drv),
            (unsigned)rnet_rb_driver_invent_count(g_rb.drv),
            (unsigned)rnet_rb_driver_promote_count(g_rb.drv),
            (unsigned long long)rnet_rb_driver_resim_ticks(g_rb.drv),
            (unsigned)g_rb.replayed_ticks,
            (unsigned)rnet_rb_driver_desync_count(g_rb.drv),
            (unsigned)rnet_rb_driver_rtt_estimate_ms(g_rb.drv),
            (unsigned)rnet_rb_driver_confirmed_through(g_rb.drv),
            rnet_rb_driver_refusal(g_rb.drv) ? rnet_rb_driver_refusal(g_rb.drv) : "none");
}

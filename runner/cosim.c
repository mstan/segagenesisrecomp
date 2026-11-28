/* cosim.c — Genesis differential co-simulation: lockstep TCP server + the
 * per-instruction checkpoint/park machinery. Compiled only under GENESIS_COSIM.
 *
 * Mirrors psxrecomp's runtime/src/cosim.c. The guest advances a monotonic
 * cycle axis (g_cosim_cycle) via GEN_COSIM_TICK on every instruction (both the
 * recompiled backend and m68k_interp). When the axis crosses g_cosim_next_cp,
 * cosim_checkpoint() hashes the FULL architectural state, records a ring row,
 * folds the cumulative chain hash, and PARKS the guest until the coordinator
 * grants step-budget over the socket. A separate OS thread runs the command
 * server, so it stays responsive while the guest (main thread / game fiber) is
 * parked. See DIFFERENTIAL-COSIMULATION.md + the GENESIS proposal.
 */
#include "cosim.h"
#include "include/genesis_runtime.h"   /* g_cpu, g_cosim_cycle/next_cp, proto */
/* The timingfields/vdpfields drill commands + the full-state hash read the own-
 * backend globals (g_machine/GVDP), which don't exist in the oracle build. */
#include "video/genesis_machine.h"     /* g_machine (master_cycle, z80_cycle_debt) */
#include "video/genesis_vdp.h"         /* GVDP fields (raster vs content breakdown) */
extern uint32_t g_68k_stamp_rebase;
extern int      glue_cosim_vint_latched(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <process.h>
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  #define closesock closesocket
  static void sock_yield(void){ Sleep(0); }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <pthread.h>
  #include <sched.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  #define closesock close
  static void sock_yield(void){ sched_yield(); }
#endif

/* --- drill accessors (owned by their TUs; text/field dumps for the coordinator) */
extern int      psg_cosim_dump(char *buf, int cap);                 /* sn76489.c */
extern int      audio_event_cosim_hist(uint64_t*, uint32_t*, uint32_t*, uint8_t*, uint8_t*, int);
extern uint64_t audio_event_cosim_hist_total(void);                 /* event_queue.c */

/* --- monotonic checkpoint axis (referenced by generated code + interp) ---- */
uint64_t g_cosim_cycle   = 0;
uint64_t g_cosim_next_cp = 0;

/* --- checkpoint / lockstep state ----------------------------------------- */
/* Two checkpoint clocks under one park mechanism:
 *   FRAME (default): keyed on g_machine.master_cycle, which machine_run_frame
 *     advances by the scanline loop — NOT by CPU execution — so it is IDENTICAL
 *     on both backends at every frame boundary regardless of how each idles the
 *     WaitForVBlank spin (the recomp fast-forwards it, a pure interp spins it).
 *     This is the correct cross-backend (A-vs-B) ruler.
 *   INSN: keyed on the monotonic g_cosim_cycle the per-instruction GEN_COSIM_TICK
 *     advances. Exact for SAME-backend determinism gates and intra-frame
 *     drill-down (where both execute identical instructions); it drifts across
 *     A-vs-B on idle-waits, so it is not the A-vs-B ruler.
 * The chain folds the checkpoint ORDINAL (both backends agree) + the state hash
 * — never the raw clock, which can differ benignly across backends. */
typedef enum { COSIM_CLK_FRAME = 0, COSIM_CLK_INSN = 1 } CosimClock;
static CosimClock s_clock    = COSIM_CLK_FRAME;
static int        s_visible  = 0;   /* pairing #2: hash guest-visible surface  */
static uint64_t s_stride    = 1;      /* frames (FRAME) or cycles (INSN) per cp */
static uint64_t s_frame_ctr = 0;      /* frames seen (FRAME mode)               */
static uint64_t s_start_frame = 0;    /* FRAME mode: free-run this many frames  */
                                      /* before the first checkpoint (skip past */
                                      /* a known-divergent prologue, e.g. the   */
                                      /* Sega scream — psxrecomp --start-cycle). */
/* s_cp is PUBLISHED LAST in do_checkpoint (after s_chain/ring are committed) and
 * is what the server waits on — so seeing the new s_cp guarantees s_chain is the
 * matching one. s_chain is volatile so the compiler can't hoist its store past
 * the s_cp publish (x86 TSO gives the store/load ordering once that's pinned). */
static volatile uint64_t s_cp    = 0; /* checkpoint ordinal (published last)    */
static volatile uint64_t s_chain = COSIM_FNV_OFFSET;  /* cumulative chain hash  */
static int      s_active    = 0;

/* last checkpoint's computed state (served while parked — no live race) */
static uint64_t       s_last_full = 0;
static CosimSubHashes s_last_sub;

/* Park control (guest = main thread; commands = server thread). Single-writer
 * discipline: the GUEST owns s_cp (monotonic checkpoint count) and s_parked; the
 * SERVER owns s_target ("let the guest run until s_cp >= s_target"). No variable
 * is written by both threads and no compound cross-written condition is tested,
 * so there is no park race (an earlier `s_parked && s_budget<=0` scheme, with
 * s_budget written by both sides, skipped checkpoints under wall-timing and
 * skewed the cp ordinals — a harness nondeterminism, not a guest one). */
static volatile uint64_t s_target = 0;    /* run while s_cp < s_target          */
static volatile int      s_parked = 0;    /* guest currently in the park spin    */

/* bounded reporting ring */
#define RING_N 8192u   /* power of two */
typedef struct { uint64_t cp, cycle, full; uint32_t pc; CosimSubHashes sub; } Row;
static Row s_ring[RING_N];

int cosim_active(void) { return s_active; }

/* ------------------------------------------------------- checkpoint + park */
/* Common body: hash full state at `clock`, fold the chain, record a ring row,
 * then park until the coordinator grants budget. */
static void do_checkpoint(uint64_t clock)
{
    /* Gate-3 fault injection is applied to live state just before hashing so
     * the very next checkpoint reflects it (proves the tool DETECTS a split). */
    cosim_state_apply_pending_injection();

    CosimSubHashes sub;
    uint64_t full = s_visible ? cosim_state_hash_visible(&sub)
                              : cosim_state_hash(&sub);

    uint64_t cp = s_cp + 1;            /* compute; do NOT publish s_cp yet */
    /* Fold the ORDINAL (both backends agree) + state hash — NOT the raw clock. */
    s_chain = cosim_fold(cosim_fold(s_chain, cp), full);
    s_last_full = full;
    s_last_sub  = sub;

    Row *r = &s_ring[cp & (RING_N - 1u)];
    r->cp = cp; r->cycle = clock; r->full = full;
    r->pc = g_cpu.PC; r->sub = sub;

    s_cp = cp;   /* PUBLISH last: the server waits on s_cp, so once it sees this
                  * value s_chain / the ring row are already committed. */

    /* Deterministic park: block here until the coordinator raises s_target past
     * this checkpoint. The guest stops at EXACTLY cp == target (it re-checks
     * after every checkpoint), so no overshoot. Never free-run + async-stop. */
    s_parked = 1;
    while (s_cp >= s_target) sock_yield();
    s_parked = 0;
}

/* INSN-mode entry: per-instruction hook (GEN_COSIM_TICK). Inert in FRAME mode
 * because cosim_init parks g_cosim_next_cp at UINT64_MAX so the macro's compare
 * never fires. */
void cosim_checkpoint(void)
{
    do_checkpoint(g_cosim_cycle);
    g_cosim_next_cp = g_cosim_cycle - (g_cosim_cycle % s_stride) + s_stride;
}

/* FRAME-mode entry: called once per wall frame (post audio drain / counter
 * reset). Checkpoints every s_stride frames, keyed on the backend-independent
 * master_cycle. No-op unless FRAME mode is active. */
void cosim_frame_checkpoint(uint64_t master_cycle)
{
    if (!s_active || s_clock != COSIM_CLK_FRAME) return;
    ++s_frame_ctr;
    if (s_frame_ctr <= s_start_frame) return;   /* free-run the prologue first  */
    if ((s_frame_ctr - s_start_frame) % s_stride) return;
    do_checkpoint(master_cycle);
}

/* ------------------------------------------------------------ socket helpers */
static int send_str(sock_t c, const char *s) {
    int n = (int)strlen(s);
    return send(c, s, n, 0) == n ? 0 : -1;
}

/* Read one '\n'-terminated line (blocking). Returns length, or -1 on close. */
static int recv_line(sock_t c, char *buf, int cap) {
    int n = 0;
    while (n < cap - 1) {
        char ch;
        int r = (int)recv(c, &ch, 1, 0);
        if (r <= 0) return -1;
        if (ch == '\n') break;
        if (ch != '\r') buf[n++] = ch;
    }
    buf[n] = 0;
    return n;
}

/* Wait for the guest to be parked at a checkpoint boundary. */
static void wait_parked(void) { while (!s_parked) sock_yield(); }

/* --------------------------------------------------------------- commands */
static void handle(sock_t c, char *line) {
    char out[2048];
    if (!strcmp(line, "status")) {
        wait_parked();
        snprintf(out, sizeof out,
            "cp %llu cycle %llu chain %016llx stride %llu parked %d\n",
            (unsigned long long)s_cp, (unsigned long long)g_cosim_cycle,
            (unsigned long long)s_chain, (unsigned long long)s_stride, s_parked);
        send_str(c, out);
    } else if (!strncmp(line, "stride ", 7)) {
        s_stride = strtoull(line + 7, 0, 10);
        send_str(c, "ok\n");
    } else if (!strncmp(line, "step", 4)) {
        long long n = (line[4] == ' ') ? strtoll(line + 5, 0, 10) : 1;
        if (n < 1) n = 1;
        wait_parked();                    /* guest parked => s_cp is stable      */
        uint64_t target = s_cp + (uint64_t)n;
        s_target = target;                /* release the guest up to `target`    */
        while (s_cp < target) sock_yield(); /* wait until it re-parks at target   */
        snprintf(out, sizeof out, "parked cp %llu cycle %llu chain %016llx\n",
            (unsigned long long)s_cp, (unsigned long long)g_cosim_cycle,
            (unsigned long long)s_chain);
        send_str(c, out);
    } else if (!strcmp(line, "chain")) {
        wait_parked();
        snprintf(out, sizeof out, "chain %016llx cp %llu cycle %llu\n",
            (unsigned long long)s_chain, (unsigned long long)s_cp,
            (unsigned long long)g_cosim_cycle);
        send_str(c, out);
    } else if (!strcmp(line, "sub")) {
        wait_parked();
        const CosimSubHashes *s = &s_last_sub;
        snprintf(out, sizeof out,
            "cpu68k %016llx timing %016llx ram %016llx z80 %016llx "
            "z80ram %016llx handshake %016llx vdp %016llx fm %016llx "
            "psg %016llx evq %016llx\n",
            (unsigned long long)s->cpu68k, (unsigned long long)s->timing,
            (unsigned long long)s->ram, (unsigned long long)s->z80,
            (unsigned long long)s->z80ram, (unsigned long long)s->handshake,
            (unsigned long long)s->vdp, (unsigned long long)s->fm,
            (unsigned long long)s->psg, (unsigned long long)s->evq);
        send_str(c, out);
    } else if (!strncmp(line, "window", 6)) {
        wait_parked();
        long long n = (line[6] == ' ') ? strtoll(line + 7, 0, 10) : 16;
        if (n < 1) n = 1; if (n > (long long)RING_N) n = RING_N;
        for (long long i = n - 1; i >= 0; i--) {
            uint64_t cp = (s_cp >= (uint64_t)i) ? s_cp - (uint64_t)i : 0;
            if (cp == 0) continue;
            Row *r = &s_ring[cp & (RING_N - 1u)];
            snprintf(out, sizeof out,
                "row cp %llu cycle %llu pc %06x full %016llx\n",
                (unsigned long long)r->cp, (unsigned long long)r->cycle,
                r->pc & 0xFFFFFFu, (unsigned long long)r->full);
            send_str(c, out);
        }
        send_str(c, "end\n");
    } else if (!strcmp(line, "cpu")) {
        wait_parked();
        int off = 0;
        off += snprintf(out+off, sizeof out-off, "pc %08x sr %04x usp %08x",
            g_cpu.PC, g_cpu.SR, g_cpu.USP);
        for (int i = 0; i < 8; i++)
            off += snprintf(out+off, sizeof out-off, " d%d %08x", i, g_cpu.D[i]);
        for (int i = 0; i < 8; i++)
            off += snprintf(out+off, sizeof out-off, " a%d %08x", i, g_cpu.A[i]);
        off += snprintf(out+off, sizeof out-off, "\n");
        send_str(c, out);
    } else if (!strncmp(line, "inject ram ", 11)) {
        unsigned addr, xorv;
        if (sscanf(line + 11, "%x %x", &addr, &xorv) == 2)
            cosim_inject_ram(addr, (uint8_t)xorv);
        send_str(c, "ok\n");
    } else if (!strncmp(line, "inject reg ", 11)) {
        int idx; unsigned xorv;
        if (sscanf(line + 11, "%d %x", &idx, &xorv) == 2)
            cosim_inject_reg(idx, xorv);
        send_str(c, "ok\n");
    } else if (!strcmp(line, "cyclefields")) {
        /* Cross-backend WORK-cycle ruler (cosim_cycles.c): per-logical-frame
         * 68K work cycles + a monotonic cumulative, same unit on both backends
         * so the coordinator diffs them directly. */
        wait_parked();
        snprintf(out, sizeof out, "work %llu cum %llu parks %llu insn %llu fb %llu\n",
            (unsigned long long)g_cosim_work_cycles,
            (unsigned long long)g_cosim_cum_cycles,
            (unsigned long long)g_cosim_park_count,
            (unsigned long long)g_cosim_work_insns,
            (unsigned long long)g_cosim_fb_count);
        send_str(c, out);
    } else if (!strcmp(line, "timingfields")) {
        wait_parked();
        snprintf(out, sizeof out,
            "audio_cyc %u stamp_rebase %u vint_latched %d master_cycle %u z80_debt %u\n",
            g_audio_cycle_counter, g_68k_stamp_rebase, glue_cosim_vint_latched(),
            g_machine.master_cycle, g_machine.z80_cycle_debt);
        send_str(c, out);
    } else if (!strcmp(line, "vdpfields")) {
        wait_parked();
        /* Separate the RASTER PHASE (scanline/status counters, which advance
         * per-scanline decoupled from the instruction axis = currency) from the
         * CONTENT (VRAM/CRAM/VSRAM/regs = real state). If only the phase differs
         * cross-backend it's benign currency like PC; if content differs it's a
         * real bug. */
        const GVDP *v = &g_machine.vdp;
        uint64_t hv = cosim_fnv_bytes(cosim_fnv_init(), v->vram, sizeof v->vram);
        uint64_t hc = cosim_fnv_bytes(cosim_fnv_init(), v->cram, sizeof v->cram);
        uint64_t hs = cosim_fnv_bytes(cosim_fnv_init(), v->vsram, sizeof v->vsram);
        uint64_t hr = cosim_fnv_bytes(cosim_fnv_init(), v->reg, sizeof v->reg);
        snprintf(out, sizeof out,
            "scanline %u in_vblank %u in_hblank %u vint_pending %u hint_ctr %u "
            "dma_active %u dma_fill %u address %u code %u ctrl_pending %u addr_hi %u "
            "vram_h %016llx cram_h %016llx vsram_h %016llx reg_h %016llx\n",
            v->scanline, v->in_vblank, v->in_hblank, v->vint_pending, v->hint_counter,
            v->dma_active, v->dma_fill_pending, v->address, v->code,
            v->control_pending, v->address_hi,
            (unsigned long long)hv, (unsigned long long)hc,
            (unsigned long long)hs, (unsigned long long)hr);
        send_str(c, out);
    } else if (!strcmp(line, "psgfields")) {
        wait_parked();
        char b[1024]; psg_cosim_dump(b, (int)sizeof b);
        send_str(c, b); send_str(c, "\n");
    } else if (!strncmp(line, "chiphist", 8)) {
        wait_parked();
        int n = (line[8] == ' ') ? atoi(line + 9) : 64;
        if (n < 1) n = 1; if (n > 4096) n = 4096;
        static uint64_t idx[4096]; static uint32_t st[4096], mc[4096];
        static uint8_t po[4096], va[4096];
        int got = audio_event_cosim_hist(idx, st, mc, po, va, n);
        snprintf(out, sizeof out, "total %llu got %d\n",
                 (unsigned long long)audio_event_cosim_hist_total(), got);
        send_str(c, out);
        for (int i = 0; i < got; i++) {
            snprintf(out, sizeof out, "h %llu %u %u %u %u\n",
                     (unsigned long long)idx[i], st[i], mc[i], po[i], va[i]);
            send_str(c, out);
        }
        send_str(c, "end\n");
    } else if (!strncmp(line, "memchunks", 9)) {
        /* Localize a cross-backend region divergence: per-chunk hashes of
         * z80ram / wram / vram (normalized accessors, comparable A-vs-B). */
        wait_parked();
        char region[32] = {0}; int nch = 32;
        sscanf(line + 9, "%31s %d", region, &nch);
        if (nch < 1) nch = 1; if (nch > 1024) nch = 1024;
        static uint64_t hh[1024];
        extern int cosim_visible_region_chunks(const char *, int, uint64_t *);
        int got = cosim_visible_region_chunks(region, nch, hh);
        if (got < 0) { send_str(c, "err region\n"); }
        else {
            snprintf(out, sizeof out, "chunks %d region %s\n", got, region);
            send_str(c, out);
            for (int i = 0; i < got; i++) {
                snprintf(out, sizeof out, "c %d %016llx\n", i, (unsigned long long)hh[i]);
                send_str(c, out);
            }
            send_str(c, "end\n");
        }
    } else if (!strcmp(line, "reset")) {
        s_chain = COSIM_FNV_OFFSET; s_cp = 0;
        cosim_state_reset();
        send_str(c, "ok\n");
    } else {
        send_str(c, "err unknown\n");
    }
}

/* -------------------------------------------------------------- server loop */
static sock_t s_listen = SOCK_INVALID;

static void server_run(void) {
    for (;;) {
        sock_t c = accept(s_listen, 0, 0);
        if (c == SOCK_INVALID) { sock_yield(); continue; }
        char line[2048];
        while (recv_line(c, line, sizeof line) >= 0) {
            if (line[0]) handle(c, line);
        }
        closesock(c);
    }
}

#ifdef _WIN32
static unsigned __stdcall server_thread(void *arg){ (void)arg; server_run(); return 0; }
#else
static void *server_thread(void *arg){ (void)arg; server_run(); return 0; }
#endif

void cosim_init(void) {
    cosim_state_reset();

    unsigned short port = 4600;
    const char *ep = getenv("GENESIS_COSIM_PORT");
    if (ep && *ep) port = (unsigned short)atoi(ep);

    /* Clock mode: frame (default, A-vs-B) or insn (same-backend / drill). */
    const char *ec = getenv("GENESIS_COSIM_CLOCK");
    s_clock = (ec && (!strcmp(ec, "insn") || !strcmp(ec, "cycle")))
                  ? COSIM_CLK_INSN : COSIM_CLK_FRAME;

    /* Pairing #2: guest-visible-surface hash (own-backend vs oracle). The oracle
     * build ALWAYS uses this (full state won't cross-compile against clownmdemu's
     * structs); the own-backend enables it via GENESIS_COSIM_VISIBLE=1. */
    const char *ev = getenv("GENESIS_COSIM_VISIBLE");
    s_visible = (ev && *ev && *ev != '0');

    const char *es = getenv("GENESIS_COSIM_STRIDE");
    s_stride = (es && *es) ? strtoull(es, 0, 10) : 1;
    if (s_stride == 0) s_stride = 1;

    /* FRAME mode: optionally free-run N frames before checkpointing (skip a
     * known-divergent prologue like the Sega scream to probe the steady state). */
    const char *esf = getenv("GENESIS_COSIM_START_FRAME");
    s_start_frame = (esf && *esf) ? strtoull(esf, 0, 10) : 0;

    g_cosim_cycle = 0;
    s_frame_ctr = 0;
    /* In FRAME mode, disarm the per-instruction hook (macro compare never
     * fires); the frame boundary drives checkpoints instead. */
    g_cosim_next_cp = (s_clock == COSIM_CLK_INSN) ? s_stride : UINT64_MAX;

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen == SOCK_INVALID) { fprintf(stderr, "[COSIM] socket() failed\n"); return; }
    int yes = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    if (bind(s_listen, (struct sockaddr*)&a, sizeof a) != 0 ||
        listen(s_listen, 1) != 0) {
        fprintf(stderr, "[COSIM] bind/listen on port %u failed\n", port);
        closesock(s_listen); s_listen = SOCK_INVALID; return;
    }


#ifdef _WIN32
    _beginthreadex(0, 0, server_thread, 0, 0, 0);
#else
    pthread_t t; pthread_create(&t, 0, server_thread, 0); pthread_detach(t);
#endif
    s_active = 1;
    fprintf(stderr, "[COSIM] listening on 127.0.0.1:%u  stride=%llu\n",
            port, (unsigned long long)s_stride);
}

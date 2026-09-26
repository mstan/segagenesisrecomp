/* fiber_snapshot_test.c — runner/fiber_compat snapshot/restore contract.
 *
 * A recursive fiber keeps live locals (a 512-byte buffer, accumulators,
 * pointers to its own stack frames) on every level and yields a value at
 * every step. The test proves:
 *   1. a snapshot taken at yield k, restored, replays the exact remaining
 *      yield sequence of an uninterrupted run (and does so repeatedly);
 *   2. restore works after the fiber ran on and overwrote its stack;
 *   3. fiber_reset re-initialises in place (same stack range), restarts from
 *      the top, and a pre-reset snapshot restored afterwards still replays;
 *   4. corrupted / truncated / foreign blobs are rejected untouched;
 *   5. snapshots are proportional to the live stack, not the reservation.
 * Exit 0 = pass. Deterministic, ROM-independent. */
#include "fiber_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEPS      400
#define MAX_DEPTH  48
#define PAD_BYTES  512

static fiber_t s_main, s_co;
static int64_t s_out;             /* value handed out at each yield */
static int     s_depth;           /* recursion depth at that yield */
static int     s_failures;

#define CHECK(cond, ...) do { if (!(cond)) { s_failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void yield_value(int64_t v, int depth)
{
    s_out = v;
    s_depth = depth;
    fiber_switch(s_main);
}

/* Each level holds a stack buffer and a pointer into its parent's frame; on
 * resume it verifies both, so any byte of the live stack that a restore got
 * wrong turns into a wrong yield value or a verification failure. */
static int64_t descend(int depth, int64_t acc, volatile unsigned char *parent_pad,
                       unsigned char parent_first)
{
    volatile unsigned char pad[PAD_BYTES];
    for (int i = 0; i < PAD_BYTES; i++)
        pad[i] = (unsigned char)(depth * 31 + i * 7 + (int)(acc & 0xFF));
    /* LCG step in unsigned arithmetic: the signed form overflows (UB), and
     * gcc/clang at -O2/-O3 exploited it (reference recursion collapsed). */
    int64_t local = (int64_t)((uint64_t)acc * 6364136223846793005ULL +
                              (uint64_t)depth + 1442695040888963407ULL);
    yield_value(local ^ (int64_t)depth, depth);
    for (int i = 0; i < PAD_BYTES; i++)
        if (pad[i] != (unsigned char)(depth * 31 + i * 7 + (int)(acc & 0xFF)))
            yield_value(-1000000 - depth, depth); /* corrupted own frame */
    if (parent_pad && parent_pad[0] != parent_first)
        yield_value(-2000000 - depth, depth); /* stale pointer-to-stack */
    if (depth < MAX_DEPTH && ((local >> 9) & 15) != 0)   /* ~15/16: deep */
        local = (int64_t)((uint64_t)local +
                          (uint64_t)descend(depth + 1, local, pad, pad[0]));
    yield_value((int64_t)((uint64_t)local + 17u * (uint64_t)depth), depth);
    return local;
}

static void fiber_entry(void *arg)
{
    int64_t seed = (int64_t)(intptr_t)arg;
    for (uint32_t round = 0;; round++)
        seed = (int64_t)((uint64_t)seed +
                         (uint64_t)descend(0, (int64_t)((uint64_t)seed + round), NULL, 0));
}

static void run(int n, int64_t *out, int *depths)
{
    for (int i = 0; i < n; i++) {
        fiber_switch(s_co);
        if (out) out[i] = s_out;
        if (depths) depths[i] = s_depth;
    }
}

static int same(const int64_t *a, const int64_t *b, int n, int *first)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) { if (first) *first = i; return 0; }
    return 1;
}

/* --overflow child: a fiber that recurses without bound must die on the
 * guard page (hardware fault), never run into the coroutine header or other
 * memory. The parent only accepts a fault-type termination. */
static volatile uint64_t s_sink;
static volatile uint64_t s_overflow_limit = ~(uint64_t)0;
static uint64_t overflow_rec(uint64_t n)
{
    volatile unsigned char big[8192];     /* > 1 page: exercises __chkstk probes */
    big[0] = (unsigned char)n;
    big[sizeof big - 1] = (unsigned char)(n >> 8);
    s_sink += big[0] + big[sizeof big - 1];
    return (n < s_overflow_limit ? overflow_rec(n + 1) : 0) + big[n & 1023];
}
/* A frame LARGER than the whole guard region: only page-by-page stack
 * probing (MSVC __chkstk; gcc/clang -fstack-clash-protection) makes it touch
 * the guard instead of jumping over it into the coroutine header. */
static uint64_t overflow_rec_huge(uint64_t n)
{
    volatile unsigned char big[2 * FIBER_GUARD_BYTES];
    big[0] = (unsigned char)n;
    s_sink += big[0];
    return (n < s_overflow_limit ? overflow_rec_huge(n + 1) : 0) + big[n & 1023];
}
static int s_overflow_huge;
static void overflow_entry(void *arg)
{
    (void)arg;
    s_sink = s_overflow_huge ? overflow_rec_huge(1) : overflow_rec(1);
    for (;;) fiber_switch(s_main);
}
#include <stdlib.h>
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <signal.h>
#  include <unistd.h>
#  include <sys/wait.h>
static uintptr_t s_guard_lo, s_guard_hi;
static void on_segv(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)uc;
    uintptr_t a = (uintptr_t)si->si_addr;
    _exit(a >= s_guard_lo && a < s_guard_hi ? 42 : 43);   /* 42 = fault in the guard */
}
#endif

static int overflow_child(void)
{
    s_main = fiber_convert_thread();
    s_co = fiber_create(1u << 20, 1u << 20, overflow_entry, NULL);
    if (!s_main || !s_co) return 2;
#if !defined(_WIN32)
    uintptr_t lo = 0, top = 0;
    fiber_stack_range(s_co, &lo, &top);
    {
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        size_t guard = FIBER_GUARD_BYTES > pg ? FIBER_GUARD_BYTES : pg;
        guard = (guard + pg - 1) / pg * pg;
        s_guard_lo = lo - guard;
    }
    s_guard_hi = lo;
    static unsigned char altstack[65536];
    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_sp = altstack;
    ss.ss_size = sizeof altstack;
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
#endif
    fiber_switch(s_co);
    return 0;   /* unreachable unless the guard failed to fault */
}

static int overflow_faults(const char *self, const char *mode)
{
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "\"%s\" %s", self, mode);
#if defined(_WIN32)
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 0;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    printf("  overflow child exit code 0x%08lX\n", (unsigned long)code);
    /* The guard itself is checked structurally in main (VirtualQuery); here
     * the child must end in a hardware fault, not run on. */
    return code == 0xC0000005u /* access violation */ || code == 0xC00000FDu /* stack overflow */ ||
           code == 0xC0000409u /* fail-fast: fault during exception dispatch */;
#else
    int st = system(cmd);
    printf("  overflow child status 0x%x\n", st);
    return st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 42;   /* faulted IN the guard */
#endif
}

int main(int argc, char **argv)
{
    static int64_t ref[STEPS], got[STEPS];
    if (argc > 1 && strcmp(argv[1], "--overflow") == 0)
        return overflow_child();
    if (argc > 1 && strcmp(argv[1], "--overflow-huge") == 0) {
        s_overflow_huge = 1;
        return overflow_child();
    }
    static int ref_depth[STEPS];

    s_main = fiber_convert_thread();
    CHECK(s_main != NULL, "fiber_convert_thread failed");
    s_co = fiber_create(1u << 20, 32u << 20, fiber_entry, (void *)(intptr_t)12345);
    CHECK(s_co != NULL, "fiber_create failed");
    if (!s_main || !s_co) return 1;

    uintptr_t lo = 0, top = 0;
    CHECK(fiber_stack_range(s_co, &lo, &top) == 0 && top - lo >= (32u << 20),
          "stack range [%p,%p) too small", (void *)lo, (void *)top);
    CHECK(fiber_stack_range(s_main, &lo, &top) != 0, "thread fiber reported a stack");
    fiber_stack_range(s_co, &lo, &top);
#if defined(_WIN32)
    {
        /* The page just below the usable stack is the no-access guard. */
        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof mbi);
        CHECK(VirtualQuery((const void *)(lo - 1), &mbi, sizeof mbi) == sizeof mbi &&
              mbi.Protect == PAGE_NOACCESS && mbi.State == MEM_COMMIT,
              "no PAGE_NOACCESS guard below the stack (protect=0x%lx)", (unsigned long)mbi.Protect);
        CHECK(VirtualQuery((const void *)lo, &mbi, sizeof mbi) == sizeof mbi &&
              mbi.Protect == PAGE_READWRITE && mbi.State == MEM_COMMIT &&
              (uintptr_t)mbi.BaseAddress + mbi.RegionSize >= top,
              "usable stack is not one committed read-write region");
    }
#endif

    /* Snapshot before the first run is legal (fresh context). */
    size_t fresh_len = fiber_snapshot_bound(s_co);
    CHECK(fresh_len > 0 && fresh_len < 4096, "fresh bound %zu", fresh_len);
    unsigned char *fresh = (unsigned char *)malloc(fresh_len);
    CHECK(fiber_snapshot_save(s_co, fresh, fresh_len) == fresh_len, "fresh save");

    /* Reference: uninterrupted run. */
    run(STEPS, ref, ref_depth);
    for (int i = 0; i < STEPS; i++) {
        int64_t v = ref[i];
        int sentinel = (v <= -1000000 && v >= -1000000 - MAX_DEPTH) ||
                       (v <= -2000000 && v >= -2000000 - MAX_DEPTH);
        CHECK(!sentinel, "reference run reported frame corruption at step %d (%lld)",
              i, (long long)v);
    }

    /* Snapshot point: the deepest yield of the reference run. */
    int k = 1;
    for (int i = 1; i < STEPS - 1; i++)
        if (ref_depth[i] > ref_depth[k]) k = i;
    CHECK(ref_depth[k] >= 16, "reference recursion too shallow (max depth %d)", ref_depth[k]);

    /* Every snapshot point replays: shallow, deepest, middle, last. */
    {
        const int points[] = { 1, k, STEPS / 2, STEPS - 1 };
        for (unsigned p = 0; p < sizeof points / sizeof points[0]; p++) {
            int kp = points[p], first = -1;
            CHECK(fiber_snapshot_load(s_co, fresh, fresh_len) == 0, "fresh load (point %d)", kp);
            run(kp, got, NULL);
            CHECK(same(ref, got, kp, &first), "prefix to %d diverged at step %d", kp, first);
            size_t plen = fiber_snapshot_bound(s_co);
            unsigned char *pb = (unsigned char *)malloc(plen);
            CHECK(fiber_snapshot_save(s_co, pb, plen) == plen, "save at %d", kp);
            for (int r = 0; r < 2; r++) {
                run(STEPS - kp, got + kp, NULL);
                CHECK(same(ref + kp, got + kp, STEPS - kp, &first),
                      "point %d rep %d diverged at step %d", kp, r, kp + first);
                CHECK(fiber_snapshot_load(s_co, pb, plen) == 0, "reload at %d", kp);
            }
            free(pb);
        }
    }

    /* Restart from the fresh snapshot: must replay the reference exactly. */
    CHECK(fiber_snapshot_load(s_co, fresh, fresh_len) == 0, "fresh load");
    run(k, got, NULL);
    int first = -1;
    CHECK(same(ref, got, k, &first), "fresh replay diverged at step %d", first);

    /* Snapshot at yield k (deep in the recursion). */
    size_t len = fiber_snapshot_bound(s_co);
    CHECK(len > sizeof(void *) * 8 && len < (4u << 20), "bound %zu not proportional", len);
    unsigned char *blob = (unsigned char *)malloc(len);
    CHECK(fiber_snapshot_save(s_co, blob, len - 1) == 0, "save accepted short buffer");
    CHECK(fiber_snapshot_save(s_co, blob, len) == len, "save at k");

    for (int rep = 0; rep < 3; rep++) {
        /* Run on (overwriting the stack), then restore and replay. */
        run(STEPS - k, got + k, NULL);
        CHECK(same(ref + k, got + k, STEPS - k, &first),
              "rep %d: post-snapshot run diverged at step %d", rep, k + first);
        CHECK(fiber_snapshot_load(s_co, blob, len) == 0, "rep %d: load", rep);
    }

    /* Rejections leave the fiber untouched. */
    {
        unsigned char *bad = (unsigned char *)malloc(len);
        memcpy(bad, blob, len);
        bad[0] ^= 0xFF;
        CHECK(fiber_snapshot_load(s_co, bad, len) == -2, "bad magic accepted");
        CHECK(fiber_snapshot_load(s_co, blob, len - 1) == -2, "truncated blob accepted");
        memcpy(bad, blob, len);
        /* header.stack_lo is the third 8-byte field (after magic+version) */
        uint64_t fake;
        memcpy(&fake, bad + 8, 8);
        fake += 4096;
        memcpy(bad + 8, &fake, 8);
        CHECK(fiber_snapshot_load(s_co, bad, len) == -3, "foreign-stack blob accepted");
        free(bad);
    }
    run(STEPS - k, got + k, NULL);
    CHECK(same(ref + k, got + k, STEPS - k, &first),
          "replay after rejected loads diverged at step %d", k + first);

    /* fiber_reset: in place, restarts from the top. */
    CHECK(fiber_reset(s_co, fiber_entry, (void *)(intptr_t)12345) == 0, "reset");
    {
        uintptr_t lo2 = 0, top2 = 0;
        fiber_stack_range(s_co, &lo2, &top2);
        CHECK(lo2 == lo && top2 == top, "reset moved the stack");
    }
    run(k, got, NULL);
    CHECK(same(ref, got, k, &first), "post-reset run diverged at step %d", first);

    /* A snapshot taken before the reset still restores after it. */
    CHECK(fiber_snapshot_load(s_co, blob, len) == 0, "load after reset");
    run(STEPS - k, got + k, NULL);
    CHECK(same(ref + k, got + k, STEPS - k, &first),
          "restore-after-reset diverged at step %d", k + first);

    /* A second fiber's blob cannot be loaded into the first. */
    {
        fiber_t other = fiber_create(1u << 20, 1u << 20, fiber_entry, (void *)(intptr_t)7);
        CHECK(other != NULL, "second fiber");
        if (other) {
            size_t olen = fiber_snapshot_bound(other);
            unsigned char *ob = (unsigned char *)malloc(olen);
            CHECK(fiber_snapshot_save(other, ob, olen) == olen, "second save");
            CHECK(fiber_snapshot_load(s_co, ob, olen) == -3, "cross-fiber blob accepted");
            free(ob);
            fiber_destroy(other);
        }
    }

    fiber_destroy(s_co);
    fiber_revert_thread();

    CHECK(overflow_faults(argv[0], "--overflow"),
          "unbounded recursion on the fiber did not fault on the guard region");
#if defined(_WIN32) || defined(GENESIS_STACK_CLASH_PROTECTION)
    /* Frames bigger than the guard: requires page-by-page stack probes. */
    CHECK(overflow_faults(argv[0], "--overflow-huge"),
          "a frame larger than the guard skipped it (stack probes missing?)");
#else
    printf("  --overflow-huge skipped: built without -fstack-clash-protection\n");
#endif
    free(blob);
    free(fresh);
    if (s_failures) {
        fprintf(stderr, "fiber_snapshot_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("fiber_snapshot_test: OK (deepest yield step %d depth %d: snapshot %zu bytes of a "
           "%zu-byte stack)\n", k, ref_depth[k], len, (size_t)(top - lo));
    return 0;
}

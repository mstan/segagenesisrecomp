/*
 * fiber_compat.h — cooperative fibers on an engine-owned, snapshottable stack.
 *
 * The runner's game scheduler (glue.c) needs a coroutine with its own C
 * stack so the recompiled 68K code can yield mid-call back to the main
 * loop for VBlank / scanline service. Rollback netplay additionally needs
 * that coroutine's execution state to be captured and restored in memory at
 * any yield point. Both needs are met by one backend on every platform:
 *
 *   - vendored minicoro (runner/external/minicoro, Unlicense/MIT-0) in its
 *     assembly mode (Win64, SysV x86-64, aarch64). On Windows its switch
 *     also swaps the TIB stack fields (StackBase/StackLimit/
 *     DeallocationStack), so __chkstk and SEH unwinding see the fiber stack.
 *   - the ENGINE owns the stack memory: one VirtualAlloc/mmap mapping per
 *     fiber, fully committed on Windows, with a no-access guard region
 *     (FIBER_GUARD_BYTES, below) between the coroutine header and the lowest
 *     usable stack byte. A fiber's stack
 *     never moves for its lifetime, so a snapshot restored into the same
 *     fiber puts every saved frame pointer / return address / pointer-to-
 *     local back at the address it was captured from.
 *
 * API mirrors the subset of the Win32 Fiber API that glue.c uses:
 *
 *   Win32                       fiber_compat
 *   --------------------------  --------------------------
 *   ConvertThreadToFiber(NULL)  fiber_convert_thread()
 *   CreateFiberEx(c,r,0,fn,arg) fiber_create(c, r, fn, arg)
 *   SwitchToFiber(f)            fiber_switch(f)
 *   DeleteFiber(f)              fiber_destroy(f)
 *   ConvertFiberToThread()      fiber_revert_thread()
 *
 * plus the rollback primitives fiber_reset / fiber_snapshot_* /
 * fiber_stack_range.
 *
 * Scheduling model: one thread fiber (the converted thread) and any number
 * of created fibers, switched strictly thread <-> created fiber (a created
 * fiber only ever switches back to the thread fiber). That is exactly the
 * glue.c scheduler's shape; any other switch aborts loudly.
 *
 * Platform prerequisite: user-mode hardware shadow stacks (Intel CET /
 * Windows "hardware-enforced stack protection") must be OFF for the
 * process, because the context switch replaces the stack pointer without a
 * matching shadow-stack switch. Runner executables link with /CETCOMPAT:NO
 * (cmake/GenesisRecompRunner.cmake) and fiber_convert_thread verifies the
 * policy at startup, aborting with a diagnostic if shadow stacks are on.
 */
#ifndef FIBER_COMPAT_H
#define FIBER_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/* Portable noreturn attribute (glue.c's trap-die helper). */
#if defined(_MSC_VER)
#  define FIBER_NORETURN __declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
#  define FIBER_NORETURN __attribute__((noreturn))
#else
#  define FIBER_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the no-access guard region under every created fiber's stack
 * (rounded up to whole pages). One 4 KB page is NOT enough: a function whose
 * frame is larger than the guard can move the stack pointer straight past it
 * and its first store then lands in the coroutine header below (minicoro's
 * saved context) instead of faulting. Measured 2026-09-25 on Linux gcc 16 /
 * clang 22 at -O2: an 8 KB frame skipped the single 4 KB guard page and
 * faulted outside it. 64 KB covers every frame the runner and generated
 * code use; gcc/clang runner builds additionally compile with
 * -fstack-clash-protection (cmake/GenesisRecompRunner.cmake), which probes
 * each page of a large frame so even a frame larger than the guard faults
 * inside it. MSVC's __chkstk already probes page by page. */
#define FIBER_GUARD_BYTES ((size_t)64u * 1024u)

/* Opaque handle to a fiber. */
typedef void *fiber_t;

/* Entry point for a created fiber. Receives the arg passed to
 * fiber_create / fiber_reset. The entry is contracted never to return
 * (glue.c loops forever switching back to the thread fiber); returning is
 * reported and aborts the process. */
typedef void (*fiber_entry_fn)(void *arg);

/* Convert the current thread into a fiber so it can fiber_switch to
 * others. Returns the handle for the current thread's fiber, or NULL on
 * failure. Call once on the thread that drives the scheduler. Aborts with a
 * diagnostic if the process runs with user shadow stacks enabled. */
fiber_t fiber_convert_thread(void);

/* Create a new fiber with its own engine-owned stack of max(commit,
 * reserve) usable bytes (rounded up to the page size), fully committed,
 * FIBER_GUARD_BYTES no-access guard below. Returns NULL on failure. The fiber does not run until
 * fiber_switch'd to. */
fiber_t fiber_create(size_t commit, size_t reserve,
                     fiber_entry_fn entry, void *arg);

/* Switch execution to target. The calling fiber is suspended until
 * something fiber_switch'es back to it. */
void fiber_switch(fiber_t target);

/* Return the currently-running fiber on this thread, or NULL when the
 * thread has not been converted. */
fiber_t fiber_current(void);

/* Destroy a fiber created by fiber_create and release its stack. Must not
 * be the currently-running fiber. */
void fiber_destroy(fiber_t fiber);

/* Revert the current thread-fiber (from fiber_convert_thread) back to a
 * plain thread and release its bookkeeping. */
void fiber_revert_thread(void);

/* ---- Rollback primitives -------------------------------------------------
 * All of these require fiber != fiber_current() (the fiber is suspended, or
 * has never run) and abort loudly otherwise: a running fiber's registers and
 * live stack cannot be captured or replaced from inside itself. */

/* Re-initialise a created fiber IN PLACE: its next switch-in starts
 * entry(arg) from the top, on the same stack mapping (no free/alloc, the
 * stack range and guard region are unchanged). Returns 0, or -1 on misuse. */
int fiber_reset(fiber_t fiber, fiber_entry_fn entry, void *arg);

/* Exact byte count fiber_snapshot_save needs for the fiber's CURRENT
 * suspended state (fixed header + saved register context + the live stack
 * span [saved sp - ABI red zone, top)). 0 if the fiber cannot be
 * snapshotted (thread fiber, NULL). */
size_t fiber_snapshot_bound(fiber_t fiber);

/* Capture the suspended fiber into dst. Returns bytes written, or 0 when
 * cap < fiber_snapshot_bound(fiber) or the fiber is not snapshottable. */
size_t fiber_snapshot_save(fiber_t fiber, void *dst, size_t cap);

/* Restore a blob produced by fiber_snapshot_save FOR THIS SAME FIBER (the
 * blob records the stack mapping it came from and is rejected for any other
 * fiber). On success the fiber resumes, at its next switch-in, exactly where
 * it was when the blob was saved. Returns 0 on success, or a negative code
 * with the fiber untouched: -1 misuse, -2 bad magic/version/size, -3 blob
 * from a different fiber/stack, -4 inconsistent stack span. */
int fiber_snapshot_load(fiber_t fiber, const void *src, size_t len);

/* The usable stack of a created fiber: [*lo, *top). Returns 0, or -1 for
 * the thread fiber / NULL. Used by rollback diagnostics (stack scans). */
int fiber_stack_range(fiber_t fiber, uintptr_t *lo, uintptr_t *top);

#ifdef __cplusplus
}
#endif

#endif /* FIBER_COMPAT_H */

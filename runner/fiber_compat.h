/*
 * fiber_compat.h — cross-platform cooperative fiber primitives.
 *
 * The runner's game scheduler (glue.c) needs a coroutine with its own C
 * stack so the recompiled 68K code can yield mid-call back to the main
 * loop for VBlank service. On Windows this is a thin wrapper over Win32
 * Fibers (CreateFiberEx/SwitchToFiber/...). On POSIX (macOS/Linux) it is
 * backed by ucontext (makecontext/swapcontext), which gives the same
 * "separate stack, cooperative switch" semantics.
 *
 * The API deliberately mirrors the subset of the Win32 Fiber API that
 * glue.c uses, so the call sites read the same on both platforms:
 *
 *   Win32                       fiber_compat
 *   --------------------------  --------------------------
 *   ConvertThreadToFiber(NULL)  fiber_convert_thread()
 *   CreateFiberEx(c,r,0,fn,arg) fiber_create(c, r, fn, arg)
 *   SwitchToFiber(f)            fiber_switch(f)
 *   DeleteFiber(f)              fiber_destroy(f)
 *   ConvertFiberToThread()      fiber_revert_thread()
 *
 * Cooperative single-threaded use only: exactly one fiber runs at a time,
 * switches are explicit. No preemption, no locking.
 */
#ifndef FIBER_COMPAT_H
#define FIBER_COMPAT_H

#include <stddef.h>

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

/* Opaque handle to a fiber. */
typedef void *fiber_t;

/* Entry point for a created fiber. Receives the arg passed to
 * fiber_create. On both backends the entry is expected never to return
 * (glue.c loops forever switching back to the main fiber); if it does
 * return, the process is left in an undefined scheduling state. */
typedef void (*fiber_entry_fn)(void *arg);

/* Convert the current thread into a fiber so it can fiber_switch to
 * others. Returns the handle for the current thread's fiber, or NULL on
 * failure. Call once on the thread that drives the scheduler. */
fiber_t fiber_convert_thread(void);

/* Create a new fiber with its own stack. commit/reserve mirror the Win32
 * CreateFiberEx parameters (initial committed / maximum reserved stack);
 * on POSIX a single stack of max(reserve, commit) bytes is allocated.
 * Returns NULL on failure. The fiber does not run until fiber_switch'd to. */
fiber_t fiber_create(size_t commit, size_t reserve,
                     fiber_entry_fn entry, void *arg);

/* Switch execution to target. The calling fiber is suspended until
 * something fiber_switch'es back to it. */
void fiber_switch(fiber_t target);

/* Return the currently-running fiber on this thread, or NULL when the
 * thread has not been converted. Useful for diagnostics that must not
 * compare stack addresses belonging to different fibers. */
fiber_t fiber_current(void);

/* Destroy a fiber created by fiber_create and free its stack. Must not be
 * the currently-running fiber. */
void fiber_destroy(fiber_t fiber);

/* Revert the current thread-fiber (from fiber_convert_thread) back to a
 * plain thread and release its bookkeeping. */
void fiber_revert_thread(void);

#ifdef __cplusplus
}
#endif

#endif /* FIBER_COMPAT_H */

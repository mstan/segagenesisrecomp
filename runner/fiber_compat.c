/*
 * fiber_compat.c — cross-platform cooperative fibers.
 *
 * Windows: thin wrappers over the Win32 Fiber API.
 * POSIX (macOS/Linux): ucontext-backed (makecontext/swapcontext).
 *
 * See fiber_compat.h for the API contract.
 */
#include "fiber_compat.h"

#if defined(_WIN32)

#include <windows.h>

fiber_t fiber_convert_thread(void)
{
    return (fiber_t)ConvertThreadToFiber(NULL);
}

fiber_t fiber_create(size_t commit, size_t reserve,
                     fiber_entry_fn entry, void *arg)
{
    /* Win32 entry is void(CALLBACK*)(LPVOID); our fiber_entry_fn is
     * void(*)(void*). Same ABI on the platforms we target. */
    return (fiber_t)CreateFiberEx((SIZE_T)commit, (SIZE_T)reserve, 0,
                                  (LPFIBER_START_ROUTINE)entry, arg);
}

void fiber_switch(fiber_t target)
{
    SwitchToFiber((LPVOID)target);
}

fiber_t fiber_current(void)
{
    return IsThreadAFiber() ? (fiber_t)GetCurrentFiber() : NULL;
}

void fiber_destroy(fiber_t fiber)
{
    if (fiber)
        DeleteFiber((LPVOID)fiber);
}

void fiber_revert_thread(void)
{
    ConvertFiberToThread();
}

#else /* POSIX: ucontext */

/* macOS deprecates the ucontext family but still ships and supports it;
 * it remains the portable way to get separate-stack coroutines. Ask for
 * the SUSv2 prototypes and silence the deprecation notice locally. */
#ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 700
#endif
#ifdef __APPLE__
#  define _DARWIN_C_SOURCE 1
#endif

#ifdef __ANDROID__
/* bionic declares the ucontext API in headers but ships no implementation
 * (makecontext/swapcontext are absent from libc). The Android build links
 * the vendored libucontext (ISC), which provides the same contract under a
 * prefix; alias it in so the branch below stays one implementation. */
#  include <libucontext/libucontext.h>
#  define ucontext_t  libucontext_ucontext_t
#  define getcontext  libucontext_getcontext
#  define makecontext libucontext_makecontext
#  define swapcontext libucontext_swapcontext
#else
#  include <ucontext.h>
#endif
#include <stdlib.h>
#include <stdint.h>
#ifndef __ANDROID__
/* bionic's signal.h pulls in sys/ucontext.h, whose struct sigcontext and
 * ucontext_t typedefs collide with the vendored libucontext's — and Android
 * only needs the SIGSTKSZ fallback below anyway. */
#include <signal.h>     /* SIGSTKSZ — minimum fiber stack floor */
#endif

/* glibc >= 2.34 no longer exposes SIGSTKSZ as a compile-time constant under
 * _XOPEN_SOURCE (it becomes sysconf(_SC_SIGSTKSZ), or is hidden entirely),
 * which breaks the build on modern Linux. It is only used below as a sane
 * lower bound on the fiber stack size, so provide a portable fallback. */
#ifndef SIGSTKSZ
#  define SIGSTKSZ 16384
#endif

#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

typedef struct fiber_impl {
    ucontext_t      ctx;
    void           *stack;      /* NULL for the thread-fiber */
    fiber_entry_fn  entry;
    void           *arg;
} fiber_impl;

/* The currently-running fiber on this thread. Cooperative + single
 * threaded, so a plain static is correct. */
static fiber_impl *s_current = NULL;

/* makecontext can only pass int-sized args, so split the fiber pointer
 * across two ints and reassemble in the trampoline. */
static void fiber_trampoline(unsigned int hi, unsigned int lo)
{
    uintptr_t p = ((uintptr_t)hi << 32) | (uintptr_t)lo;
    fiber_impl *f = (fiber_impl *)p;
    f->entry(f->arg);
    /* entry is contracted never to return. If it does, there is no
     * sensible context to resume (uc_link is NULL), so abort loudly
     * rather than fall off the end of the stack into undefined behavior. */
    abort();
}

fiber_t fiber_convert_thread(void)
{
    fiber_impl *f = (fiber_impl *)calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->stack = NULL;            /* runs on the real thread stack */
    s_current = f;
    return (fiber_t)f;
}

fiber_t fiber_create(size_t commit, size_t reserve,
                     fiber_entry_fn entry, void *arg)
{
    size_t stack_size = reserve > commit ? reserve : commit;
    if (stack_size < SIGSTKSZ)
        stack_size = SIGSTKSZ;

    fiber_impl *f = (fiber_impl *)calloc(1, sizeof(*f));
    if (!f)
        return NULL;

    f->stack = malloc(stack_size);
    if (!f->stack) {
        free(f);
        return NULL;
    }
    f->entry = entry;
    f->arg   = arg;

    if (getcontext(&f->ctx) != 0) {
        free(f->stack);
        free(f);
        return NULL;
    }
    f->ctx.uc_stack.ss_sp   = f->stack;
    f->ctx.uc_stack.ss_size = stack_size;
    f->ctx.uc_link          = NULL;   /* entry never returns; see trampoline */

    uintptr_t p = (uintptr_t)f;
    makecontext(&f->ctx, (void (*)(void))fiber_trampoline, 2,
                (unsigned int)(p >> 32), (unsigned int)(p & 0xFFFFFFFFu));
    return (fiber_t)f;
}

void fiber_switch(fiber_t target)
{
    fiber_impl *to   = (fiber_impl *)target;
    fiber_impl *from = s_current;
    if (to == from)
        return;
    s_current = to;
    swapcontext(&from->ctx, &to->ctx);
    /* Resumed: s_current was restored to `from` by whoever switched back. */
}

fiber_t fiber_current(void)
{
    return (fiber_t)s_current;
}

void fiber_destroy(fiber_t fiber)
{
    fiber_impl *f = (fiber_impl *)fiber;
    if (!f)
        return;
    free(f->stack);
    free(f);
}

void fiber_revert_thread(void)
{
    if (s_current && s_current->stack == NULL) {
        free(s_current);
        s_current = NULL;
    }
}

#endif /* _WIN32 */

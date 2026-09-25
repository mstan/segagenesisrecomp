/*
 * fiber_compat.c — cooperative fibers on an engine-owned, snapshottable stack.
 *
 * One backend on every platform: vendored minicoro in assembly mode, with
 * the coroutine header and stack placed by us (mco_init on our own mapping)
 * so the stack never moves and a guard page sits under it. See
 * fiber_compat.h for the API contract and the snapshot semantics.
 *
 * Mapping layout of a created fiber (low -> high addresses):
 *
 *   map                        mco_coro + _mco_context (+ minicoro "storage"
 *                              padding, never used) — header_size bytes
 *   map + header_size          guard region, no access (FIBER_GUARD_BYTES
 *                              rounded up to whole pages; see fiber_compat.h)
 *   stack_lo = +guard          lowest usable stack byte
 *   stack_top                  one past the highest stack byte; the stack
 *                              grows down from here
 *
 * minicoro sees [guard, stack_top) as its stack; its overflow check and our
 * TIB StackLimit (Windows) are both pinned to stack_lo so a __chkstk probe
 * that crosses the limit touches the guard region and faults.
 */
#include "fiber_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* minicoro configuration: assembly switch everywhere we build (Win64,
 * SysV x86-64, aarch64 — minicoro #errors on anything else), no heap
 * allocator (we place the coroutine ourselves), errors reported loudly. */
#define MCO_USE_ASM
#define MCO_NO_DEFAULT_ALLOCATOR
#define MCO_LOG(s) fprintf(stderr, "[fiber] minicoro: %s\n", (s))
#define MINICORO_IMPL
#include "external/minicoro/minicoro.h"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#  if defined(__linux__) && defined(__x86_64__)
#    include <sys/syscall.h>
#  endif
#endif

/* AddressSanitizer builds: a snapshot legitimately reads and writes stack
 * bytes that ASan's shadow currently marks as other frames' redzones (the
 * shadow reflects whatever ran last on those addresses, not the frames the
 * blob describes). Unpoison the saved/restored span so neither the copy nor
 * the resumed frames report false positives. (Incompatible with ASan's
 * detect_stack_use_after_return, which moves locals off the fiber stack.) */
#ifdef _MCO_USE_ASAN
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
#  define FIBER_ASAN_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
#  define FIBER_ASAN_UNPOISON(p, n) ((void)0)
#endif

#define FIBER_IMPL_MAGIC   0x46494252u   /* 'FIBR' */
#define FIBER_SNAP_MAGIC   0x46534E50u   /* 'FSNP' */
#define FIBER_SNAP_VERSION 1u
/* Bytes below the saved stack pointer included in a snapshot. The switch
 * is a real call, so nothing below sp is live, but the SysV / Apple arm64
 * ABIs define a 128-byte red zone; saving a conservative margin makes the
 * blob independent of that reasoning at negligible cost. */
#define FIBER_SNAP_RED_ZONE 256u

typedef struct fiber_impl {
    uint32_t       magic;
    int            is_thread;     /* the converted thread (no mapping) */
    mco_coro      *co;            /* created fibers: lives at map[0] */
    fiber_entry_fn entry;
    void          *arg;
    unsigned char *map;
    size_t         map_size;
    size_t         header_size;   /* bytes before the guard page */
    size_t         page;
    size_t         guard_size;    /* bytes of no-access guard below stack_lo */
    uintptr_t      stack_lo;
    uintptr_t      stack_top;
} fiber_impl;

typedef struct FiberSnapHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t stack_lo;     /* identity of the mapping the blob came from */
    uint64_t stack_top;
    uint64_t span_lo;      /* first saved stack byte */
    uint64_t span_len;     /* saved bytes [span_lo, stack_top) */
    uint32_t ctx_size;     /* sizeof(_mco_ctxbuf) */
    uint32_t reserved;
} FiberSnapHeader;

/* Cooperative and single-threaded: plain statics are correct. */
static fiber_impl *s_thread  = NULL;
static fiber_impl *s_current = NULL;

static FIBER_NORETURN void fiber_fatal(const char *what)
{
    fprintf(stderr, "[fiber] FATAL: %s\n", what);
    fflush(stderr);
    abort();
}

static size_t align_up(size_t v, size_t a) { return (v + (a - 1)) & ~(a - 1); }

static size_t os_page_size(void)
{
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize ? (size_t)si.dwPageSize : 4096u;
#else
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? (size_t)p : 4096u;
#endif
}

/* ---- platform prerequisite: no user shadow stacks ----------------------- */

static void fiber_require_no_shadow_stack(void)
{
#if defined(_WIN32)
    /* Resolved at run time and with the ABI spelled out locally, so the check
     * does not depend on the SDK / _WIN32_WINNT the runner is compiled with:
     * PROCESS_MITIGATION_POLICY::ProcessUserShadowStackPolicy == 13, and
     * bit 0 of PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY.Flags is
     * EnableUserShadowStack. Pre-Windows-8 kernels lack the API: no CET. */
    typedef BOOL (WINAPI *GetPolicyFn)(HANDLE, int, PVOID, SIZE_T);
    GetPolicyFn get_policy = NULL;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32) {
        FARPROC fp = GetProcAddress(k32, "GetProcessMitigationPolicy");
        memcpy(&get_policy, &fp, sizeof get_policy);
    }
    DWORD flags = 0;
    if (get_policy && get_policy(GetCurrentProcess(), 13, &flags, sizeof flags) &&
        (flags & 1u)) {
        fiber_fatal("user-mode hardware shadow stacks (CET) are enabled for this "
                    "process; the game fiber's context switch is incompatible with "
                    "them. Link the runner with /CETCOMPAT:NO "
                    "(cmake/GenesisRecompRunner.cmake) and do not force "
                    "hardware-enforced stack protection for this executable.");
    }
#elif defined(__linux__) && defined(__x86_64__)
#  ifndef ARCH_SHSTK_STATUS
#    define ARCH_SHSTK_STATUS 0x5005
#  endif
    unsigned long long features = 0;
    if (syscall(SYS_arch_prctl, ARCH_SHSTK_STATUS, &features) == 0 && (features & 1ull))
        fiber_fatal("user shadow stacks (CET SHSTK) are enabled for this process; "
                    "the game fiber's context switch is incompatible with them. "
                    "Build the runner with -fcf-protection=none or disable "
                    "glibc.cpu.x86_shstk.");
#endif
}

/* ---- mapping ------------------------------------------------------------- */

static unsigned char *os_map(size_t size, size_t guard_off, size_t guard_size)
{
#if defined(_WIN32)
    unsigned char *m = (unsigned char *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT,
                                                     PAGE_READWRITE);
    if (!m) return NULL;
    DWORD old = 0;
    if (!VirtualProtect(m + guard_off, guard_size, PAGE_NOACCESS, &old)) {
        VirtualFree(m, 0, MEM_RELEASE);
        return NULL;
    }
    return m;
#else
    void *m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return NULL;
    if (mprotect((unsigned char *)m + guard_off, guard_size, PROT_NONE) != 0) {
        munmap(m, size);
        return NULL;
    }
    return (unsigned char *)m;
#endif
}

static void os_unmap(unsigned char *m, size_t size)
{
    if (!m) return;
#if defined(_WIN32)
    (void)size;
    VirtualFree(m, 0, MEM_RELEASE);
#else
    munmap(m, size);
#endif
}

/* ---- minicoro glue -------------------------------------------------------- */

static void fiber_mco_entry(mco_coro *co)
{
    fiber_impl *f = (fiber_impl *)mco_get_user_data(co);
    f->entry(f->arg);
    fiber_fatal("fiber entry returned (it is contracted to loop forever)");
}

static uintptr_t ctx_saved_sp(const _mco_ctxbuf *c)
{
#if defined(__x86_64__) || defined(_M_X64)
    return (uintptr_t)c->rsp;
#elif defined(__aarch64__)
    return (uintptr_t)c->sp;
#else
#  error "fiber_compat: unsupported architecture (minicoro asm: x86-64, aarch64)"
#endif
}

/* (Re)initialise f->co in place at the start of f's mapping. */
static int fiber_init_coro(fiber_impl *f)
{
    mco_coro *co = (mco_coro *)f->map;
    size_t context_addr = _mco_align_forward((size_t)co + sizeof(mco_coro), 16);
    size_t storage_addr = _mco_align_forward(context_addr + sizeof(_mco_context), 16);
    size_t guard_addr   = (size_t)f->map + f->header_size;
    if (storage_addr > guard_addr) return -1;

    mco_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.func         = fiber_mco_entry;
    desc.user_data    = f;
    /* minicoro places its stack right after the storage area; size the
     * (unused) storage so the stack starts exactly at the guard page. */
    desc.storage_size = guard_addr - storage_addr;
    desc.coro_size    = f->map_size;
    desc.stack_size   = (size_t)(f->stack_top - guard_addr);
    if (mco_init(co, &desc) != MCO_SUCCESS) return -1;
    if ((size_t)co->stack_base != guard_addr) return -1;

#if defined(_WIN32)
    /* Pin the TIB limit above the guard page: __chkstk then probes the guard
     * (and faults) instead of silently walking into the coroutine header. */
    {
        _mco_context *ctx = (_mco_context *)co->context;
        ctx->ctx.stack_limit   = (void *)f->stack_lo;
        ctx->ctx.dealloc_stack = (void *)guard_addr;
    }
#endif
    f->co = co;
    return 0;
}

static fiber_impl *as_created(fiber_t fiber, const char *op)
{
    fiber_impl *f = (fiber_impl *)fiber;
    if (!f || f->magic != FIBER_IMPL_MAGIC || f->is_thread || !f->co)
        return NULL;
    if (f == s_current) {
        char msg[128];
        snprintf(msg, sizeof msg, "%s on the currently running fiber", op);
        fiber_fatal(msg);
    }
    return f;
}

/* ---- public API ------------------------------------------------------------ */

fiber_t fiber_convert_thread(void)
{
    fiber_require_no_shadow_stack();
    if (s_thread)
        return (fiber_t)s_thread;
    fiber_impl *f = (fiber_impl *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->magic = FIBER_IMPL_MAGIC;
    f->is_thread = 1;
    s_thread = f;
    s_current = f;
    return (fiber_t)f;
}

fiber_t fiber_create(size_t commit, size_t reserve, fiber_entry_fn entry, void *arg)
{
    if (!entry) return NULL;
    size_t page = os_page_size();
    size_t stack_size = reserve > commit ? reserve : commit;
    if (stack_size < MCO_MIN_STACK_SIZE) stack_size = MCO_MIN_STACK_SIZE;
    stack_size = align_up(stack_size, page);
    size_t header = align_up(_mco_align_forward(sizeof(mco_coro), 16) +
                             _mco_align_forward(sizeof(_mco_context), 16) + 16, page);

    size_t guard = align_up(FIBER_GUARD_BYTES > page ? FIBER_GUARD_BYTES : page, page);

    fiber_impl *f = (fiber_impl *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->magic       = FIBER_IMPL_MAGIC;
    f->entry       = entry;
    f->arg         = arg;
    f->page        = page;
    f->guard_size  = guard;
    f->header_size = header;
    f->map_size    = header + guard + stack_size;
    f->map         = os_map(f->map_size, header, guard);
    if (!f->map) { free(f); return NULL; }
    f->stack_lo  = (uintptr_t)(f->map + header + guard);
    f->stack_top = (uintptr_t)(f->map + f->map_size);
    if (fiber_init_coro(f) != 0) {
        os_unmap(f->map, f->map_size);
        free(f);
        return NULL;
    }
    return (fiber_t)f;
}

void fiber_switch(fiber_t target)
{
    fiber_impl *to = (fiber_impl *)target;
    fiber_impl *from = s_current;
    if (!to || to == from)
        return;
    if (!from || to->magic != FIBER_IMPL_MAGIC)
        fiber_fatal("fiber_switch with an unconverted thread or invalid target");
    if (from->is_thread && !to->is_thread) {
        s_current = to;
        mco_result r = mco_resume(to->co);
        s_current = from;
        if (r != MCO_SUCCESS)
            fiber_fatal(mco_result_description(r));
    } else if (!from->is_thread && to->is_thread) {
        /* s_current is set back to `from` by whoever resumes us. */
        mco_result r = mco_yield(from->co);
        if (r != MCO_SUCCESS)
            fiber_fatal(mco_result_description(r));
    } else {
        fiber_fatal("unsupported switch (only thread <-> created fiber)");
    }
}

fiber_t fiber_current(void)
{
    return (fiber_t)s_current;
}

void fiber_destroy(fiber_t fiber)
{
    fiber_impl *f = as_created(fiber, "fiber_destroy");
    if (!f) return;
    mco_uninit(f->co);
    os_unmap(f->map, f->map_size);
    f->magic = 0;
    free(f);
}

void fiber_revert_thread(void)
{
    if (s_thread && s_current == s_thread) {
        s_thread->magic = 0;
        free(s_thread);
        s_thread = NULL;
        s_current = NULL;
    }
}

int fiber_reset(fiber_t fiber, fiber_entry_fn entry, void *arg)
{
    fiber_impl *f = as_created(fiber, "fiber_reset");
    if (!f || !entry) return -1;
    if (mco_uninit(f->co) != MCO_SUCCESS) return -1;
    f->entry = entry;
    f->arg   = arg;
    if (fiber_init_coro(f) != 0)
        fiber_fatal("fiber_reset could not re-initialise the coroutine in place");
    return 0;
}

static int snap_span(const fiber_impl *f, uintptr_t *span_lo)
{
    if (f->co->state != MCO_SUSPENDED) return -1;
    const _mco_context *ctx = (const _mco_context *)f->co->context;
    uintptr_t sp = ctx_saved_sp(&ctx->ctx);
    if (sp < f->stack_lo || sp > f->stack_top) return -1;
    uintptr_t lo = sp - FIBER_SNAP_RED_ZONE;
    if (sp < f->stack_lo + FIBER_SNAP_RED_ZONE) lo = f->stack_lo;
    *span_lo = lo;
    return 0;
}

size_t fiber_snapshot_bound(fiber_t fiber)
{
    fiber_impl *f = as_created(fiber, "fiber_snapshot_bound");
    uintptr_t lo;
    if (!f || snap_span(f, &lo) != 0) return 0;
    return sizeof(FiberSnapHeader) + sizeof(_mco_ctxbuf) + (size_t)(f->stack_top - lo);
}

size_t fiber_snapshot_save(fiber_t fiber, void *dst, size_t cap)
{
    fiber_impl *f = as_created(fiber, "fiber_snapshot_save");
    uintptr_t lo;
    if (!f || !dst || snap_span(f, &lo) != 0) return 0;
    size_t span = (size_t)(f->stack_top - lo);
    size_t need = sizeof(FiberSnapHeader) + sizeof(_mco_ctxbuf) + span;
    if (cap < need) return 0;
    FiberSnapHeader h;
    memset(&h, 0, sizeof h);
    h.magic     = FIBER_SNAP_MAGIC;
    h.version   = FIBER_SNAP_VERSION;
    h.stack_lo  = (uint64_t)f->stack_lo;
    h.stack_top = (uint64_t)f->stack_top;
    h.span_lo   = (uint64_t)lo;
    h.span_len  = (uint64_t)span;
    h.ctx_size  = (uint32_t)sizeof(_mco_ctxbuf);
    unsigned char *out = (unsigned char *)dst;
    memcpy(out, &h, sizeof h);
    memcpy(out + sizeof h, &((const _mco_context *)f->co->context)->ctx, sizeof(_mco_ctxbuf));
    FIBER_ASAN_UNPOISON((const void *)lo, span);
    memcpy(out + sizeof h + sizeof(_mco_ctxbuf), (const void *)lo, span);
    return need;
}

int fiber_snapshot_load(fiber_t fiber, const void *src, size_t len)
{
    fiber_impl *f = as_created(fiber, "fiber_snapshot_load");
    if (!f || !src) return -1;
    if (f->co->state != MCO_SUSPENDED) return -1;
    if (len < sizeof(FiberSnapHeader) + sizeof(_mco_ctxbuf)) return -2;
    FiberSnapHeader h;
    memcpy(&h, src, sizeof h);
    if (h.magic != FIBER_SNAP_MAGIC || h.version != FIBER_SNAP_VERSION ||
        h.ctx_size != sizeof(_mco_ctxbuf) ||
        len != sizeof h + sizeof(_mco_ctxbuf) + h.span_len)
        return -2;
    if (h.stack_lo != (uint64_t)f->stack_lo || h.stack_top != (uint64_t)f->stack_top)
        return -3;
    if (h.span_lo < h.stack_lo || h.span_lo > h.stack_top ||
        h.span_lo + h.span_len != h.stack_top)
        return -4;
    _mco_ctxbuf ctx;
    const unsigned char *in = (const unsigned char *)src;
    memcpy(&ctx, in + sizeof h, sizeof ctx);
    uintptr_t sp = ctx_saved_sp(&ctx);
    if (sp < (uintptr_t)h.span_lo || sp > (uintptr_t)h.stack_top)
        return -4;
    FIBER_ASAN_UNPOISON((void *)(uintptr_t)h.span_lo, (size_t)h.span_len);
    memcpy((void *)(uintptr_t)h.span_lo, in + sizeof h + sizeof ctx, (size_t)h.span_len);
    memcpy(&((_mco_context *)f->co->context)->ctx, &ctx, sizeof ctx);
    f->co->state   = MCO_SUSPENDED;
    f->co->prev_co = NULL;
    return 0;
}

int fiber_stack_range(fiber_t fiber, uintptr_t *lo, uintptr_t *top)
{
    fiber_impl *f = (fiber_impl *)fiber;
    if (!f || f->magic != FIBER_IMPL_MAGIC || f->is_thread) return -1;
    if (lo)  *lo  = f->stack_lo;
    if (top) *top = f->stack_top;
    return 0;
}

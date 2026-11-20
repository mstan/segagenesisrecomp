/*
 * console_capture.c -- see console_capture.h.
 *
 * Platform assumptions (documented per the portability policy):
 *   - Windows: UCRT low-level I/O. _dup2 onto fds 1/2 also updates the
 *     process STD_OUTPUT/STD_ERROR handles for console-subsystem programs.
 *     Pipes are created _O_NOINHERIT so a child process does not keep a
 *     write end alive; fds 1/2 themselves remain inheritable, so a child that
 *     outlives the game can delay EOF -- shutdown waits a bounded time only.
 *   - POSIX: pipe/dup2/pthreads. The read end and the saved originals are
 *     close-on-exec. Linux raises the pipe to 1 MiB when F_SETPIPE_SZ exists.
 *   - Anything else: the fallback in console_capture.h (no pipes, no ring).
 * Nothing here touches simulation state; it is host-only I/O.
 */
#include "console_capture.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#  define CC_PIPES 1
#  define CC_DUP 1
#  define cc_dup     _dup
#  define cc_dup2    _dup2
#  define cc_close   _close
#  define cc_read(f, b, n)  _read((f), (b), (unsigned)(n))
#  define cc_write(f, b, n) _write((f), (b), (unsigned)(n))
#  define cc_fileno  _fileno
#  define CC_NULL_DEVICE "NUL"
#elif defined(__unix__) || defined(__APPLE__)
#  include <fcntl.h>
#  include <pthread.h>
#  include <unistd.h>
#  define CC_PIPES 1
#  define CC_DUP 1
#  define cc_dup     dup
#  define cc_dup2    dup2
#  define cc_close   close
#  define cc_read    read
#  define cc_write   write
#  define cc_fileno  fileno
#  define CC_NULL_DEVICE "/dev/null"
#else
#  define CC_PIPES 0
#  define CC_DUP 0
#endif

/* Defined in crash_report.c (part of every runner executable; tests that
 * link this file alone define it themselves). Its fatal path calls it. */
extern void (*crash_report_pre_exit_hook)(void);

#define CC_RING_BYTES (1u << 20)   /* ~1 MiB of timestamped history */
#define CC_LINE_MAX   4096

/* ---- lock + clock ------------------------------------------------------- */
#if defined(_WIN32)
static SRWLOCK s_lock = SRWLOCK_INIT;
static void cc_lock(void)   { AcquireSRWLockExclusive(&s_lock); }
static void cc_unlock(void) { ReleaseSRWLockExclusive(&s_lock); }
static double cc_now(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static void cc_sleep_ms(int ms) { Sleep((DWORD)ms); }
#elif CC_PIPES
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static void cc_lock(void)   { pthread_mutex_lock(&s_lock); }
static void cc_unlock(void) { pthread_mutex_unlock(&s_lock); }
static double cc_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static void cc_sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#else
static void cc_lock(void) {}
static void cc_unlock(void) {}
static double cc_now(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }
#endif

/* ---- state -------------------------------------------------------------- */
typedef struct CcStream {
    int    target_fd;        /* 1 or 2 */
    int    read_fd;          /* pipe read end, -1 when not capturing */
    int    orig_fd;          /* saved original target, -1 if it had none */
    char   line[CC_LINE_MAX];
    size_t len;
    volatile int done;       /* reader reached EOF */
#if defined(_WIN32)
    HANDLE thread;
#elif CC_PIPES
    pthread_t thread;
    int    thread_ok;
#endif
} CcStream;

static CcStream s_streams[2];
static int    s_installed, s_active, s_shut;
static double s_t0;
static char   s_ring[CC_RING_BYTES];
static unsigned long long s_ring_total;
static FILE  *s_file;
static char   s_path[1024];
/* Fallback: fds 1/2 pointed straight at the file. */
static int    s_fb_redirected, s_fb_saved[2] = { -1, -1 };

/* The player's home folder ("C:\Users\<name>", "/home/<name>") is replaced
 * by "<home>" in recorded lines: engine and library messages print absolute
 * save/settings paths, and the file is meant to be sent to someone. Both
 * slash spellings; case-insensitive on Windows. The console echo is raw. */
static char   s_home[2][512];
static size_t s_home_len;

static void home_init(void)
{
#if defined(_WIN32)
    const char *h = getenv("USERPROFILE");
#else
    const char *h = getenv("HOME");
#endif
    size_t n;
    if (!h || !h[0]) return;
    n = strlen(h);
    while (n > 1 && (h[n - 1] == '\\' || h[n - 1] == '/')) --n;
    if (n < 4 || n >= sizeof s_home[0]) return;   /* "/" or "C:\": nothing to hide */
    memcpy(s_home[0], h, n);
    memcpy(s_home[1], h, n);
    s_home[0][n] = s_home[1][n] = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s_home[0][i] == '/') s_home[0][i] = '\\';
        if (s_home[1][i] == '\\') s_home[1][i] = '/';
    }
    s_home_len = n;
}

/* `home` at `p`, ending at a path boundary: "C:\Users\Matt" must not
 * match the start of "C:\Users\Matthew". */
static int home_at(const char *p, size_t left, const char *home)
{
    char next;
    if (left < s_home_len) return 0;
#if defined(_WIN32)
    if (_strnicmp(p, home, s_home_len) != 0) return 0;
#else
    if (strncmp(p, home, s_home_len) != 0) return 0;
#endif
    if (left == s_home_len) return 1;
    next = p[s_home_len];
    return !((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
             (next >= '0' && next <= '9') || next == '_' || next == '-' || next == '.');
}

/* Copy `n` bytes of `in` to `out` with the home folder redacted. */
static size_t redact(const char *in, size_t n, char *out, size_t cap)
{
    static const char tag[] = "<home>";
    size_t o = 0;
    for (size_t i = 0; i < n && o < cap;) {
        if (s_home_len && (home_at(in + i, n - i, s_home[0]) || home_at(in + i, n - i, s_home[1]))) {
            for (size_t k = 0; tag[k] && o < cap; ++k) out[o++] = tag[k];
            i += s_home_len;
        } else {
            out[o++] = in[i++];
        }
    }
    return o;
}

double console_capture_uptime(void) { return s_t0 > 0.0 ? cc_now() - s_t0 : 0.0; }
int console_capture_active(void) { return s_active; }
unsigned long long console_capture_bytes_recorded(void)
{
    unsigned long long n;
    cc_lock();
    n = s_ring_total;
    cc_unlock();
    return n;
}
const char *console_capture_file_path(void) { return s_path; }

/* ---- ring + file (call with the lock held) ------------------------------- */
static void ring_put(const char *p, size_t n)
{
    while (n) {
        size_t at = (size_t)(s_ring_total % CC_RING_BYTES);
        size_t k = CC_RING_BYTES - at;
        if (k > n) k = n;
        memcpy(s_ring + at, p, k);
        s_ring_total += k;
        p += k;
        n -= k;
    }
}

static void emit_line_locked(const char *raw, size_t raw_n)
{
    static char text[CC_LINE_MAX + 64];   /* lock held: one user at a time */
    char prefix[32];
    int pn = snprintf(prefix, sizeof prefix, "[%10.3f] ", console_capture_uptime());
    size_t n;
    while (raw_n && (raw[raw_n - 1] == '\r' || raw[raw_n - 1] == '\n')) --raw_n;
    n = redact(raw, raw_n, text, sizeof text);
    if (pn < 0) pn = 0;
    ring_put(prefix, (size_t)pn);
    ring_put(text, n);
    ring_put("\n", 1);
    if (s_file) {
        fwrite(prefix, 1, (size_t)pn, s_file);
        fwrite(text, 1, n, s_file);
        fputc('\n', s_file);
    }
}

/* Oldest retained bytes first, starting on a line boundary when wrapped. */
static void ring_dump_locked(FILE *f)
{
    unsigned long long total = s_ring_total;
    unsigned long long avail = total < CC_RING_BYTES ? total : CC_RING_BYTES;
    unsigned long long start = total - avail;
    if (start) {
        while (avail && s_ring[start % CC_RING_BYTES] != '\n') { ++start; --avail; }
        if (avail) { ++start; --avail; }
        fprintf(f, "[ earlier console output was dropped: %llu bytes ]\n", start);
    }
    while (avail) {
        size_t at = (size_t)(start % CC_RING_BYTES);
        size_t k = CC_RING_BYTES - at;
        if (k > avail) k = (size_t)avail;
        fwrite(s_ring + at, 1, k, f);
        start += k;
        avail -= k;
    }
}

#if CC_PIPES
/* ---- reader thread ------------------------------------------------------ */
static void write_all(int fd, const char *p, size_t n)
{
    while (n) {
        int w = (int)cc_write(fd, p, n);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return;   /* console gone: the ring and file still record */
        }
        p += w;
        n -= (size_t)w;
    }
}

static void consume(CcStream *st, const char *p, size_t n)
{
    cc_lock();
    for (size_t i = 0; i < n; ++i) {
        char c = p[i];
        if (c == '\n' || st->len == CC_LINE_MAX - 1) {
            if (c != '\n') st->line[st->len++] = c;
            emit_line_locked(st->line, st->len);
            st->len = 0;
        } else {
            st->line[st->len++] = c;
        }
    }
    if (s_file) fflush(s_file);
    cc_unlock();
}

static void reader_loop(CcStream *st)
{
    char buf[8192];
    for (;;) {
        int n = (int)cc_read(st->read_fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (st->orig_fd >= 0) write_all(st->orig_fd, buf, (size_t)n);
        consume(st, buf, (size_t)n);
    }
    cc_lock();
    if (st->len) { emit_line_locked(st->line, st->len); st->len = 0; }
    if (s_file) fflush(s_file);
    cc_unlock();
    st->done = 1;
}

#if defined(_WIN32)
static DWORD WINAPI reader_main(LPVOID arg) { reader_loop((CcStream *)arg); return 0; }
static int make_pipe(int fds[2]) { return _pipe(fds, 1u << 20, _O_BINARY | _O_NOINHERIT); }
static void set_cloexec(int fd) { (void)fd; }
static int start_reader(CcStream *st)
{
    st->thread = CreateThread(NULL, 0, reader_main, st, 0, NULL);
    return st->thread != NULL;
}
#else
static void *reader_main(void *arg) { reader_loop((CcStream *)arg); return NULL; }
static void set_cloexec(int fd)
{
    if (fd >= 0) fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}
static int make_pipe(int fds[2])
{
    if (pipe(fds) != 0) return -1;
#if defined(F_SETPIPE_SZ)
    (void)fcntl(fds[1], F_SETPIPE_SZ, 1 << 20);
#endif
    return 0;
}
static int start_reader(CcStream *st)
{
    st->thread_ok = pthread_create(&st->thread, NULL, reader_main, st) == 0;
    if (st->thread_ok) pthread_detach(st->thread);
    return st->thread_ok;
}
#endif

/* Point `target_fd` back at its original (or the null device). */
static void restore_target(CcStream *st)
{
    if (st->orig_fd >= 0) {
        cc_dup2(st->orig_fd, st->target_fd);
    } else {
        FILE *nul = fopen(CC_NULL_DEVICE, "wb");
        if (nul) { cc_dup2(cc_fileno(nul), st->target_fd); fclose(nul); }
    }
}
#endif /* CC_PIPES */

/* Fatal path (crash_report_dump_persistent): this thread may hold any lock,
 * including ours or the CRT's, so take none and close nothing. Pointing fds
 * 1/2 back at their originals closes the pipes' write ends; the readers,
 * which flush the file after every chunk, drain to EOF. Bounded wait: if a
 * reader is blocked (this thread holds the lock, a child holds a write end)
 * the report still has everything up to the last flushed chunk. */
static void crash_hook(void)
{
#if CC_PIPES
    if (!s_active || s_shut) return;
    s_shut = 1;
    for (int i = 0; i < 2; ++i)
        if (s_streams[i].orig_fd >= 0) cc_dup2(s_streams[i].orig_fd, s_streams[i].target_fd);
    for (int waited = 0; waited < 500 && !(s_streams[0].done && s_streams[1].done); waited += 5)
        cc_sleep_ms(5);
    s_active = 0;
#endif
}

int console_capture_install(void)
{
    if (s_installed) return s_active;
    s_installed = 1;
    s_t0 = cc_now();
    home_init();
    s_streams[0].read_fd = s_streams[1].read_fd = -1;
    s_streams[0].orig_fd = s_streams[1].orig_fd = -1;
    {
        const char *e = getenv("GENESIS_CONSOLE_CAPTURE");
        if (e && e[0] == '0') return 0;
    }
#if CC_PIPES
    fflush(stdout);
    fflush(stderr);
#if defined(_WIN32)
    /* A GUI-subsystem program has no console streams (_fileno() < 0, writes
     * dropped). Give them real descriptors so the redirect has a target. */
    if (_fileno(stdout) < 0) (void)freopen(CC_NULL_DEVICE, "w", stdout);
    if (_fileno(stderr) < 0) (void)freopen(CC_NULL_DEVICE, "w", stderr);
#endif
    int ok = 1, started = 0;
    for (int i = 0; i < 2 && ok; ++i) {
        CcStream *st = &s_streams[i];
        int p[2];
        st->target_fd = cc_fileno(i ? stderr : stdout);
        if (st->target_fd < 0 || make_pipe(p) != 0) { ok = 0; break; }
        st->orig_fd = cc_dup(st->target_fd);   /* -1: no console; still capture */
        set_cloexec(st->orig_fd);
        set_cloexec(p[0]);
        /* POSIX dup2 returns the fd and UCRT _dup2 returns 0; both -1 on error. */
        if (cc_dup2(p[1], st->target_fd) < 0) {
            cc_close(p[0]);
            cc_close(p[1]);
            if (st->orig_fd >= 0) cc_close(st->orig_fd);
            st->orig_fd = -1;
            ok = 0;
            break;
        }
        cc_close(p[1]);
        st->read_fd = p[0];
        if (!start_reader(st)) {
            restore_target(st);
            cc_close(st->read_fd);
            st->read_fd = -1;
            ok = 0;
            break;
        }
        ++started;
    }
    if (!ok) {
        /* Leave nothing half-redirected: undo the streams already started.
         * Their readers see EOF and exit on their own. */
        for (int i = 0; i < started; ++i) restore_target(&s_streams[i]);
        return 0;
    }
#if defined(_WIN32)
    /* A pipe makes the CRT fully buffer stdout; Windows has no line
     * buffering, so keep it unbuffered (console behaviour). */
    setvbuf(stdout, NULL, _IONBF, 0);
#else
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
#endif
    s_active = 1;
    crash_report_pre_exit_hook = crash_hook;
    atexit(console_capture_shutdown);
    return 1;
#else
    return 0;
#endif
}

static void rotate(const char *path)
{
    char prev[1100];
    snprintf(prev, sizeof prev, "%s.previous", path);
    remove(prev);
    rename(path, prev);
}

int console_capture_open_file(const char *path, const char *header)
{
    FILE *f;
    if (!path || !*path) return 0;
    console_capture_close_file();
    fflush(stdout);
    fflush(stderr);
    rotate(path);
    f = fopen(path, "wb");
    if (!f) return 0;
    if (header) fputs(header, f);
    cc_lock();
    if (s_active) {
        fputs("---- console output since launch ----\n", f);
        ring_dump_locked(f);
        fputs("---- live ----\n", f);
    } else {
        fputs("---- console output from here on (no launch history on this platform) ----\n", f);
    }
    fflush(f);
    s_file = f;
    snprintf(s_path, sizeof s_path, "%s", path);
    cc_unlock();
#if CC_DUP
    if (!s_active) {
        /* Fallback: send fds 1/2 straight to the file. Shares its offset. */
        for (int i = 0; i < 2; ++i) {
            int fd = i ? 2 : 1;
            if (s_fb_saved[i] < 0) s_fb_saved[i] = cc_dup(fd);
            cc_dup2(cc_fileno(f), fd);
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        s_fb_redirected = 1;
    }
#endif
    return 1;
}

void console_capture_close_file(void)
{
    fflush(stdout);
    fflush(stderr);
#if CC_DUP
    if (s_fb_redirected) {
        for (int i = 0; i < 2; ++i) {
            int fd = i ? 2 : 1;
            if (s_fb_saved[i] >= 0) cc_dup2(s_fb_saved[i], fd);
        }
        s_fb_redirected = 0;
    }
#endif
    cc_lock();
    if (s_file) {
        fflush(s_file);
        fclose(s_file);
        s_file = NULL;
    }
    s_path[0] = 0;
    cc_unlock();
}

void console_capture_shutdown(void)
{
    if (s_shut) return;
    s_shut = 1;
    fflush(stdout);
    fflush(stderr);
#if CC_PIPES
    if (s_active) {
        /* Closing the last write ends lets each reader drain to EOF. A child
         * process that inherited fd 1/2 can hold them open: wait bounded. */
        for (int i = 0; i < 2; ++i) restore_target(&s_streams[i]);
        for (int waited = 0; waited < 1000 && !(s_streams[0].done && s_streams[1].done); waited += 5)
            cc_sleep_ms(5);
        s_active = 0;
    }
#endif
    console_capture_close_file();
}

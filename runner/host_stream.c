#include "host_stream.h"
#include "engine_worker.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct GenesisHostStream {
    FILE *file;
    GenesisWorker *worker;
    struct GenesisHostStream *next;
    size_t capacity, used, pending_size;
    unsigned active;
    int job_ok, failed, io_failed, inflight, pending_flush;
    unsigned char *buffer[2], *pending;
};
/* Stream creation/destruction and the exit handler share the single producer
 * thread. Workers only access their published file/pending/job_ok fields. */
static GenesisHostStream *s_streams;
/* All captures share one bounded I/O lane. A new publication joins the prior
 * job through GenesisWorker; unrelated captures must not each wake a thread
 * for every frame while competing with the game and audio workers. The lane
 * lives until its last borrowed stream is drained and unregistered. */
static GenesisWorker *s_stream_worker;
static int s_exit_registered;

static void write_job(void *context)
{
    GenesisHostStream *s = context;
    int ok = fwrite(s->pending, 1, s->pending_size, s->file) == s->pending_size
             && !ferror(s->file);
    if (s->pending_flush && fflush(s->file)) ok = 0;
    s->job_ok = ok;
}
static int join_job(GenesisHostStream *s)
{
    if (s->inflight) {
        genesis_worker_join(s->worker);
        if (!s->job_ok) s->failed = s->io_failed = 1;
        s->pending_size = 0;
        s->inflight = 0;
    }
    return !s->io_failed;
}
static int publish(GenesisHostStream *s, int flush)
{
    if (!join_job(s)) return 0;
    if (!s->used && !flush) return 1;
    s->pending = s->buffer[s->active];
    s->pending_size = s->used;
    s->active ^= 1u;
    s->used = 0;
    s->job_ok = 0;
    s->inflight = 1;
    s->pending_flush = flush;
    genesis_worker_start(s->worker, write_job, s);
    return 1;
}
static void drain_at_exit(void)
{
    while (s_streams)
        if (!genesis_host_stream_destroy(s_streams))
            fprintf(stderr, "[host-stream] file write failed at exit\n");
}
GenesisHostStream *genesis_host_stream_create(FILE *file, size_t capacity)
{
    if (!file || !capacity || capacity > (SIZE_MAX - sizeof(GenesisHostStream))/2)
        return NULL;
    GenesisHostStream *s = calloc(1, sizeof *s + capacity*2);
    if (!s) return NULL;
    if (!s_exit_registered) {
        if (atexit(drain_at_exit)) { free(s); return NULL; }
        s_exit_registered = 1;
    }
    s->file = file; s->capacity = capacity;
    s->buffer[0] = (unsigned char *)(s+1);
    s->buffer[1] = s->buffer[0] + capacity;
    if (!s_streams) s_stream_worker = genesis_worker_create();
    s->worker = s_stream_worker;
    s->next = s_streams; s_streams = s;
    return s;
}
int genesis_host_stream_write(GenesisHostStream *s, const void *data, size_t size)
{
    if (!s || (!data && size) || s->failed) return 0;
    const unsigned char *p = data;
    while (size) {
        size_t n = s->capacity - s->used;
        if (n > size) n = size;
        memcpy(s->buffer[s->active]+s->used, p, n);
        s->used += n; p += n; size -= n;
        if (s->used == s->capacity && !publish(s, 0)) return 0;
    }
    return 1;
}
int genesis_host_stream_vprintf(GenesisHostStream *s, const char *format, va_list args)
{
    if (!s || !format || s->failed) return 0;
    char local[512];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(local, sizeof local, format, copy);
    va_end(copy);
    if (n < 0) { s->failed = 1; return 0; }
    char *text = local;
    if ((size_t)n >= sizeof local) {
        text = malloc((size_t)n+1);
        if (!text) { s->failed = 1; return 0; }
        va_copy(copy, args);
        if (vsnprintf(text, (size_t)n+1, format, copy) != n) {
            va_end(copy); free(text); s->failed = 1; return 0;
        }
        va_end(copy);
    }
    int ok = genesis_host_stream_write(s, text, (size_t)n);
    if (text != local) free(text);
    return ok;
}
int genesis_host_stream_printf(GenesisHostStream *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ok = genesis_host_stream_vprintf(s, format, args);
    va_end(args);
    return ok;
}
int genesis_host_stream_fprintf(GenesisHostStream *s, FILE *file, const char *format, ...)
{
    va_list args; va_start(args, format);
    int ok = s ? genesis_host_stream_vprintf(s, format, args) : vfprintf(file, format, args) >= 0;
    va_end(args); return ok;
}
int genesis_host_stream_flush_async(GenesisHostStream *s)
{
    return s && publish(s, 1) && !s->failed;
}
int genesis_host_stream_flush(GenesisHostStream *s)
{
    if (!s) return 1;
    int ok = publish(s, 0);
    if (!join_job(s)) ok = 0;
    if (fflush(s->file) || ferror(s->file)) { s->failed = 1; ok = 0; }
    return ok && !s->failed;
}
int genesis_host_stream_flush_all(void)
{
    int ok=1;
    for(GenesisHostStream *s=s_streams;s;s=s->next)
        if(!genesis_host_stream_flush(s))ok=0;
    return ok;
}
int genesis_host_stream_destroy(GenesisHostStream *s)
{
    if (!s) return 1;
    int ok = genesis_host_stream_flush(s);
    GenesisHostStream **at = &s_streams;
    while (*at && *at != s) at = &(*at)->next;
    if (*at) *at = s->next;
    if (!s_streams) {
        genesis_worker_destroy(s_stream_worker);
        s_stream_worker = NULL;
    }
    free(s);
    return ok;
}

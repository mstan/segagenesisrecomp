#define SDL_MAIN_HANDLED
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

/* Single translation unit: checks the production implementations,
 * including ownership state. No production substitutions except an I/O gate
 * around the actual fwrite, used to make backpressure deterministic. */
#include "../../runner/engine_worker.c"
typedef struct Gate {
    SDL_sem *started, *allow;
    SDL_atomic_t enabled;
} Gate;
static SDL_threadID main_thread;
static Gate stream_gate, persist_gate;
static FILE *gated_file;
static SDL_atomic_t fail_flush;
static void gate_wait(Gate *g)
{
    if (SDL_ThreadID() != main_thread && SDL_AtomicCAS(&g->enabled, 1, 0)) {
        SDL_SemPost(g->started);
        SDL_SemWait(g->allow);
    }
}
static size_t controlled_fwrite(const void *p, size_t n, size_t count, FILE *f)
{
    if (f == gated_file) gate_wait(&stream_gate);
    return fwrite(p, n, count, f);
}
static int controlled_fflush(FILE *f)
{
    if (f == gated_file && SDL_AtomicCAS(&fail_flush, 1, 0)) return EOF;
    return fflush(f);
}
static int fail_format_allocation;
static void *format_malloc(size_t size)
{
    if (fail_format_allocation) { fail_format_allocation = 0; return NULL; }
    return malloc(size);
}
#define fwrite controlled_fwrite
#define fflush controlled_fflush
#define malloc format_malloc
#include "../../runner/host_stream.c"
#undef malloc
#undef fflush
#undef fwrite
#include "../../runner/persist_writer.c"
#include "../../runner/host_atomic_file.c"
#include "../../runner/cd/backup.c"

static unsigned checks;
static void fixture_fail(int line,const char *condition)
{
    fprintf(stderr,"FAIL line %d: %s\n",line,condition);
    /* A failed assertion must not leave our synthetic sink gates closed while
     * production atexit correctly drains accepted writes. */
    SDL_AtomicSet(&stream_gate.enabled,0);SDL_AtomicSet(&persist_gate.enabled,0);
    if(stream_gate.allow)SDL_SemPost(stream_gate.allow);
    if(persist_gate.allow)SDL_SemPost(persist_gate.allow);
    exit(2);
}
#define CHECK(c) do { ++checks; if (!(c)) { \
    fixture_fail(__LINE__,#c); \
} } while (0)
static void init_gate(Gate *g)
{
    g->started = SDL_CreateSemaphore(0); g->allow = SDL_CreateSemaphore(0);
    CHECK(g->started && g->allow);
}
#if GENESIS_ENGINE_WORKERS
static int release_later(void *context)
{
    SDL_Delay(40); SDL_SemPost(((Gate *)context)->allow); return 0;
}
static SDL_Thread *release_thread(Gate *g)
{
    SDL_Thread *t = SDL_CreateThread(release_later, "test release", g);
    CHECK(t != NULL); return t;
}
static void wait_started(Gate *g)
{
    CHECK(SDL_SemWaitTimeout(g->started, 5000) == 0);
}
#endif
static void exact_file(FILE *f, const void *expected, size_t n)
{
    unsigned char *p = malloc(n ? n : 1); CHECK(p != NULL);
    CHECK(fflush(f) == 0); CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fread(p, 1, n, f) == n); CHECK(fgetc(f) == EOF);
    CHECK(!memcmp(p, expected, n)); free(p);
    CHECK(fseek(f, 0, SEEK_END) == 0);
}
static void repeat_vprintf(GenesisHostStream *s, const char *format, ...)
{
    va_list args; va_start(args, format);
    CHECK(genesis_host_stream_vprintf(s, format, args));
    CHECK(genesis_host_stream_vprintf(s, format, args));
    va_end(args);
}
static void live_text(const char *path,const char *expected)
{
    FILE *reader=fopen(path,"rb");CHECK(reader);
    char got[128];size_t n=fread(got,1,sizeof got,reader);CHECK(!ferror(reader));
    CHECK(n==strlen(expected)&&!memcmp(got,expected,n));CHECK(!fclose(reader));
}
static void stream_extension(const char *dir)
{
    char path[1024];snprintf(path,sizeof path,"%s/live-reader.txt",dir);
    FILE *f=fopen(path,"wb");CHECK(f);gated_file=f;
    char stdio_buffer[8192];CHECK(!setvbuf(f,stdio_buffer,_IOFBF,sizeof stdio_buffer));
    GenesisHostStream *s=genesis_host_stream_create(f,17);CHECK(s);
#if GENESIS_ENGINE_WORKERS
    CHECK(s->worker);SDL_AtomicSet(&stream_gate.enabled,1);
#endif
    CHECK(genesis_host_stream_fprintf(s,f,"%s:%d\n","row",1));
    CHECK(genesis_host_stream_flush_async(s));
#if GENESIS_ENGINE_WORKERS
    wait_started(&stream_gate);CHECK(s->inflight&&s->pending_size==6);
    live_text(path,""); /* No accidental producer fflush. */
    SDL_SemPost(stream_gate.allow);genesis_worker_join(s->worker);
#endif
    live_text(path,"row:1\n"); /* Visible solely from background fflush. */
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&stream_gate.enabled,1);
#endif
    CHECK(genesis_host_stream_fprintf(s,f,"%s:%d\n","row",2));
    CHECK(genesis_host_stream_flush_async(s));
#if GENESIS_ENGINE_WORKERS
    wait_started(&stream_gate);live_text(path,"row:1\n");
    uint64_t start=SDL_GetPerformanceCounter();SDL_Thread *release=release_thread(&stream_gate);
#endif
    CHECK(genesis_host_stream_write(s,"abcdefghijklmnopq",17));
#if GENESIS_ENGINE_WORKERS
    CHECK(1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()>=20.0);
    SDL_WaitThread(release,NULL);
#endif
    /* Exact-full writes leave used==0 but still need a worker fflush. */
#if GENESIS_ENGINE_WORKERS
    /* Settle the exact-full data job before arming a gate for the following
     * zero-byte flush. Otherwise that earlier job can consume the gate while
     * flush_async is joining it, before this test can release anything. */
    genesis_worker_join(s->worker);
    SDL_AtomicSet(&stream_gate.enabled,1);
#endif
    CHECK(genesis_host_stream_flush_async(s));
#if GENESIS_ENGINE_WORKERS
    wait_started(&stream_gate);CHECK(s->inflight&&s->pending_size==0);
    start=SDL_GetPerformanceCounter();release=release_thread(&stream_gate);
#endif
    CHECK(genesis_host_stream_destroy(s));
#if GENESIS_ENGINE_WORKERS
    CHECK(1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()>=20.0);
    SDL_WaitThread(release,NULL);
#endif
    CHECK(!fclose(f));gated_file=NULL;
    live_text(path,"row:1\nrow:2\nabcdefghijklmnopq");

    f=tmpfile();CHECK(f);s=genesis_host_stream_create(f,17);CHECK(s);gated_file=f;
    CHECK(genesis_host_stream_write(s,"prior",5));CHECK(genesis_host_stream_flush(s));
    SDL_AtomicSet(&fail_flush,1);
    CHECK(genesis_host_stream_flush_async(s)); /* Acceptance, even on inline fallback. */
    CHECK(!genesis_host_stream_flush(s));CHECK(!genesis_host_stream_write(s,"!",1));
    CHECK(!genesis_host_stream_destroy(s));exact_file(f,"prior",5);CHECK(!fclose(f));gated_file=NULL;

    f=tmpfile();CHECK(f);
    CHECK(genesis_host_stream_fprintf(NULL,f,"%s:%d%c","fallback",42,0));
    exact_file(f,"fallback:42",12); /* Includes the explicit NUL byte. */
    CHECK(!fclose(f));CHECK(!s_streams);
}
static void stream_checks(void)
{
    CHECK(genesis_host_stream_create(NULL, 1) == NULL);
    FILE *f = tmpfile(); CHECK(f != NULL);
    CHECK(genesis_host_stream_create(f, 0) == NULL);
    CHECK(genesis_host_stream_create(f, SIZE_MAX) == NULL);
    GenesisHostStream *s = genesis_host_stream_create(f, 17); CHECK(s);
#if GENESIS_ENGINE_WORKERS
    CHECK(s->worker != NULL); /* A fallback is not an async test. */
#else
    CHECK(s->worker == NULL);
#endif
    unsigned char a[17], b[17], expected[34];
    memset(a, 'A', sizeof a); memset(b, 'B', sizeof b);
    memcpy(expected, a, 17); memcpy(expected + 17, b, 17);
    gated_file = f;
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&stream_gate.enabled, 1);
#endif
    CHECK(genesis_host_stream_write(s, a, sizeof a));
#if GENESIS_ENGINE_WORKERS
    wait_started(&stream_gate);
    CHECK(genesis_worker_busy(s->worker));
    CHECK(!memcmp(s->pending, expected, 17));
#endif
    memset(a, 'Z', sizeof a); /* Caller mutation cannot change pending bytes. */
    CHECK(genesis_host_stream_write(s, b, 8));
#if GENESIS_ENGINE_WORKERS
    uint64_t start = SDL_GetPerformanceCounter();
    SDL_Thread *release = release_thread(&stream_gate);
#endif
    CHECK(genesis_host_stream_write(s, b + 8, 9));
#if GENESIS_ENGINE_WORKERS
    double ms = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
    CHECK(ms >= 20.0); SDL_WaitThread(release, NULL);
    printf("stream backpressure_ms=%.3f\n", ms);
#endif
    memset(b, 'Z', sizeof b);
    CHECK(genesis_host_stream_flush(s)); exact_file(f, expected, sizeof expected);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(genesis_host_stream_write(s, "HEAD", 4));
    CHECK(genesis_host_stream_flush(s)); memcpy(expected, "HEAD", 4);
    exact_file(f, expected, sizeof expected);
    CHECK(genesis_host_stream_destroy(s)); CHECK(s_streams == NULL);
    CHECK(fclose(f) == 0); gated_file = NULL;

    f = tmpfile(); CHECK(f); s = genesis_host_stream_create(f, 31); CHECK(s);
    unsigned char large[8193];
    for (size_t i = 0; i < sizeof large; ++i) large[i] = (unsigned char)(i * 29u);
    CHECK(genesis_host_stream_write(s, large, sizeof large));
    memset(large, 0, sizeof large);
    CHECK(genesis_host_stream_destroy(s));
    for (size_t i = 0; i < sizeof large; ++i) large[i] = (unsigned char)(i * 29u);
    exact_file(f, large, sizeof large);
    s = genesis_host_stream_create(f, 19); CHECK(s);
    char text[1201]; memset(text, 'q', sizeof text - 1); text[1200] = 0;
    CHECK(genesis_host_stream_printf(s, "%s:%d%c", text, 42, 0));
    CHECK(genesis_host_stream_destroy(s));
    CHECK(fseek(f, (long)sizeof large, SEEK_SET) == 0);
    char got[1204]; CHECK(fread(got, 1, sizeof got, f) == sizeof got);
    CHECK(!memcmp(got, text, 1200) && !memcmp(got + 1200, ":42", 3) && got[1203] == 0);
    CHECK(fclose(f) == 0);
    f = tmpfile(); CHECK(f); s = genesis_host_stream_create(f, 29); CHECK(s);
    repeat_vprintf(s, "%s:%d", "copy", 42);
    CHECK(genesis_host_stream_destroy(s)); exact_file(f, "copy:42copy:42", 14);
    CHECK(!fclose(f));


}
static unsigned char durable[64];
static char callback_path[128];
static SDL_atomic_t fail_write;
static unsigned writes;
static int fake_atomic_write(const char *path, const void *p, size_t n)
{
    gate_wait(&persist_gate);
    ++writes;
    if (SDL_AtomicGet(&fail_write)) return 0;
    if (n != sizeof durable) return 0;
    memcpy(durable, p, n); snprintf(callback_path, sizeof callback_path, "%s", path);
    return 1;
}
static GenesisPersistWriter *make_persist(void)
{
    unsigned char initial[64]; memset(initial, 'A', sizeof initial);
    char path[] = "immutable-progress.srm";
    memcpy(durable, initial, sizeof durable);
    GenesisPersistWriter *p = genesis_persist_create(path, initial, sizeof initial, fake_atomic_write);
    CHECK(p); memset(initial, 'Z', sizeof initial); memset(path, 'Z', sizeof path - 1);
    CHECK(p->committed[0] == 'A'); CHECK(!strcmp(p->path, "immutable-progress.srm"));
    return p;
}
static void persist_checks(void)
{
    unsigned char live[64], latest[64]; memset(live, 'B', sizeof live); memset(latest, 'C', sizeof latest);
    CHECK(!genesis_persist_create("", live, sizeof live, fake_atomic_write));
    CHECK(!genesis_persist_create("x", live, SIZE_MAX, fake_atomic_write));
    GenesisPersistWriter *p = make_persist();
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&persist_gate.enabled, 1);
#endif
    CHECK(genesis_persist_submit(p, live) == 1);
    memset(live, 'Z', sizeof live);
#if GENESIS_ENGINE_WORKERS
    CHECK(p->worker != NULL); wait_started(&persist_gate);
    CHECK(genesis_persist_poll(p) == 1); CHECK(p->inflight);
    CHECK(p->committed[0] == 'A' && p->pending[0] == 'B');
    CHECK(genesis_persist_submit(p, latest) == 0);
    SDL_Thread *release = release_thread(&persist_gate);
#endif
    CHECK(genesis_persist_flush(p, latest));
#if GENESIS_ENGINE_WORKERS
    SDL_WaitThread(release, NULL);
#endif
    CHECK(!memcmp(durable, latest, sizeof durable)); CHECK(!memcmp(p->committed, latest, sizeof durable));
    CHECK(!strcmp(callback_path, "immutable-progress.srm"));
    unsigned before = writes; CHECK(genesis_persist_submit(p, latest) == 1); CHECK(writes == before);
    CHECK(genesis_persist_destroy(p));

    /* Destroy drains the accepted immutable snapshot, not mutated caller data. */
    p = make_persist(); memset(live, 'D', sizeof live);
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&persist_gate.enabled, 1);
#endif
    CHECK(genesis_persist_submit(p, live) == 1); memset(live, 'E', sizeof live);
#if GENESIS_ENGINE_WORKERS
    wait_started(&persist_gate); release = release_thread(&persist_gate);
#endif
    CHECK(genesis_persist_destroy(p));
#if GENESIS_ENGINE_WORKERS
    SDL_WaitThread(release, NULL);
#endif
    CHECK(durable[0] == 'D' && durable[63] == 'D');

    p = make_persist(); SDL_AtomicSet(&fail_write, 1); memset(live, 'F', sizeof live);
    int accepted = genesis_persist_submit(p, live); CHECK(accepted == 1 || accepted == -1);
    CHECK(!genesis_persist_flush(p, live)); CHECK(!genesis_persist_poll(p));
    CHECK(p->committed[0] == 'A' && durable[0] == 'A');
    CHECK(genesis_persist_submit(p, latest) == -1); CHECK(!genesis_persist_destroy(p));
    SDL_AtomicSet(&fail_write, 0);
}
typedef struct AtomicJob { char path[1024]; unsigned char payload[8192]; int ok; } AtomicJob;
static int atomic_job(void *context)
{
    AtomicJob *j = context; j->ok = 1;
    for (unsigned i = 0; i < 16; ++i) {
        j->payload[0] = (unsigned char)i;
        if (!segacd_atomic_file_store(j->path, j->payload, sizeof j->payload)) j->ok = 0;
    }
    return 0;
}
static void real_file_checks(const char *dir)
{
    char path[1024]; snprintf(path, sizeof path, "%s/readonly.bin", dir);
    FILE *f = fopen(path, "wb"); CHECK(f); CHECK(fwrite("old", 1, 3, f) == 3); CHECK(!fclose(f));
    f = fopen(path, "rb"); CHECK(f);
    GenesisHostStream *s = genesis_host_stream_create(f, 2); CHECK(s);
    (void)genesis_host_stream_write(s, "new", 3);
    CHECK(!genesis_host_stream_flush(s)); CHECK(!genesis_host_stream_write(s, "!", 1));
    CHECK(!genesis_host_stream_destroy(s)); clearerr(f);
    CHECK(fseek(f, 0, SEEK_SET) == 0); char old[3]; CHECK(fread(old, 1, 3, f) == 3);
    CHECK(!memcmp(old, "old", 3)); CHECK(!fclose(f));

    AtomicJob jobs[2]; memset(jobs, 0, sizeof jobs);
    SDL_Thread *threads[2];
    for (int i = 0; i < 2; ++i) {
        snprintf(jobs[i].path, sizeof jobs[i].path, "%s/atomic-%d.srm", dir, i);
        memset(jobs[i].payload, 'G' + i, sizeof jobs[i].payload);
        threads[i] = SDL_CreateThread(atomic_job, "atomic file fixture", &jobs[i]); CHECK(threads[i]);
    }
    for (int i = 0; i < 2; ++i) {
        SDL_WaitThread(threads[i], NULL); CHECK(jobs[i].ok);
        unsigned char got[8192]; CHECK(segacd_backup_load(jobs[i].path, got) == SEGACD_BACKUP_OK);
        CHECK(!memcmp(got, jobs[i].payload, sizeof got));
    }
#ifdef _WIN32
    /* Replacement denied: previous progress survives and temporary is removed. */
    HANDLE hold = CreateFileA(jobs[0].path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(hold != INVALID_HANDLE_VALUE);
    unsigned char changed[8192]; memset(changed, 'X', sizeof changed);
    CHECK(!segacd_atomic_file_store(jobs[0].path, changed, sizeof changed)); CloseHandle(hold);
    unsigned char unchanged[8192]; CHECK(segacd_backup_load(jobs[0].path, unchanged) == SEGACD_BACKUP_OK);
    CHECK(!memcmp(unchanged, jobs[0].payload, sizeof unchanged));
#endif
    snprintf(path, sizeof path, "%s/absent-parent/file.srm", dir);
    CHECK(!segacd_atomic_file_store(path, "x", 1));
}
int main(int argc, char **argv)
{
    CHECK(SDL_Init(0) == 0); main_thread = SDL_ThreadID();
    init_gate(&stream_gate); init_gate(&persist_gate);
    if(argc==3&&!strcmp(argv[1],"--stream-extension")) {
        stream_extension(argv[2]);
        printf("PASS stream extension workers=%d checks=%u\n",GENESIS_ENGINE_WORKERS,checks);
        SDL_Quit();return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--format-failure")) {
        FILE *f = tmpfile(); CHECK(f);
        GenesisHostStream *s = genesis_host_stream_create(f, 1024); CHECK(s);
        CHECK(genesis_host_stream_write(s, "accepted-prefix", 15));
        char huge[601]; memset(huge, 'x', sizeof huge - 1); huge[600] = 0;
        fail_format_allocation = 1;
        CHECK(!genesis_host_stream_printf(s, "%s", huge));
        CHECK(!genesis_host_stream_destroy(s));
        exact_file(f, "accepted-prefix", 15); CHECK(!fclose(f));
        puts("PASS accepted prefix drained after formatting allocation failure");
        SDL_Quit(); return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--exit-stream")) {
        FILE *f = fopen(argv[2], "wb"); CHECK(f);
        GenesisHostStream *s = genesis_host_stream_create(f, 17); CHECK(s);
        unsigned char data[12345];
        for (size_t i = 0; i < sizeof data; ++i) data[i] = (unsigned char)(i * 31u);
        CHECK(genesis_host_stream_write(s, data, sizeof data));
        if (argc > 3) SDL_Quit();
        return 0; /* Production atexit must drain before CRT closes FILE. */
    }
    CHECK(argc == 2);
    stream_extension(argv[1]); stream_checks(); persist_checks(); real_file_checks(argv[1]);
    CHECK(s_streams == NULL);
    SDL_DestroySemaphore(stream_gate.started); SDL_DestroySemaphore(stream_gate.allow);
    SDL_DestroySemaphore(persist_gate.started); SDL_DestroySemaphore(persist_gate.allow);
    printf("PASS workers=%d checks=%u\n", GENESIS_ENGINE_WORKERS, checks);
    SDL_Quit(); return 0;
}

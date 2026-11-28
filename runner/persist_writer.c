#include "persist_writer.h"
#include "engine_worker.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct GenesisPersistWriter {
    GenesisWorker *worker;
    GenesisPersistWrite write;
    GenesisPersistCompleted completed;
    void *completed_context;
    char *path;
    size_t size;
    unsigned char *committed, *pending;
    int inflight, job_ok, failed, worker_attempted;
};
static void persist_job(void *context)
{
    GenesisPersistWriter *p = context;
    p->job_ok = p->write(p->path, p->pending, p->size);
}
GenesisPersistWriter *genesis_persist_create(const char *path, const void *initial,
                                             size_t size, GenesisPersistWrite write)
{
    if (!path || !*path || !initial || !size || !write ||
        size > (SIZE_MAX-sizeof(GenesisPersistWriter))/2) return NULL;
    GenesisPersistWriter *p = calloc(1, sizeof *p + size*2);
    if (!p) return NULL;
    size_t length = strlen(path);
    if (length == SIZE_MAX) { free(p); return NULL; }
    p->path = malloc(length+1);
    if (!p->path) { free(p); return NULL; }
    memcpy(p->path, path, length+1);
    p->size = size; p->write = write;
    p->committed = (unsigned char *)(p+1);
    p->pending = p->committed + size;
    memcpy(p->committed, initial, size);
    return p;
}
int genesis_persist_poll(GenesisPersistWriter *p)
{
    if (!p) return 0;
    if (p->inflight && !genesis_worker_busy(p->worker)) {
        genesis_worker_join(p->worker);
        p->inflight = 0;
        if (!p->job_ok) p->failed = 1;
        else {
            memcpy(p->committed, p->pending, p->size);
            if(p->completed)p->completed(p->path,p->committed,p->size,p->completed_context);
        }
    }
    return !p->failed;
}
void genesis_persist_on_completed(GenesisPersistWriter *p, GenesisPersistCompleted completed, void *context)
{
    if(p){p->completed=completed;p->completed_context=context;}
}
static int submit(GenesisPersistWriter *p, const void *data, int force)
{
    if (!p || !data || !genesis_persist_poll(p)) return -1;
    if (p->inflight) return 0;
    if (!force && !memcmp(p->committed, data, p->size)) return 1;
    if (!p->worker_attempted) {
        p->worker_attempted = 1; p->worker = genesis_worker_create();
    }
    memcpy(p->pending, data, p->size);
    p->job_ok = 0; p->inflight = 1;
    genesis_worker_start(p->worker, persist_job, p);
    return genesis_persist_poll(p) ? 1 : -1;
}
int genesis_persist_submit(GenesisPersistWriter *p, const void *data)
{return submit(p,data,0);}
int genesis_persist_submit_force(GenesisPersistWriter *p, const void *data)
{return submit(p,data,1);}
int genesis_persist_wait(GenesisPersistWriter *p)
{
    if(!p)return 0;
    genesis_worker_join(p->worker);return genesis_persist_poll(p);
}
int genesis_persist_flush(GenesisPersistWriter *p, const void *latest)
{
    if (!p || !latest) return 0;
    genesis_worker_join(p->worker);
    if (!genesis_persist_poll(p) || genesis_persist_submit(p, latest) < 0) return 0;
    genesis_worker_join(p->worker);
    return genesis_persist_poll(p);
}
int genesis_persist_destroy(GenesisPersistWriter *p)
{
    if (!p) return 1;
    genesis_worker_join(p->worker);
    int ok = genesis_persist_poll(p);
    genesis_worker_destroy(p->worker);
    free(p->path); free(p);
    return ok;
}

/* One persistent host worker. A posted job owns its inputs until join; no job
 * may outlive the caller's same-tick dependency boundary. No SDL renderer work. */
#ifndef GENESIS_ENGINE_WORKER_H
#define GENESIS_ENGINE_WORKER_H
#include <stdint.h>
typedef struct GenesisWorker GenesisWorker;
typedef void (*GenesisWorkFunction)(void *);
GenesisWorker *genesis_worker_create(void);
/* NULL worker executes synchronously. A non-NULL worker first joins any prior
 * job, publishes this one exactly once, then returns without waiting. */
int genesis_worker_start(GenesisWorker *,GenesisWorkFunction,void *);
void genesis_worker_join(GenesisWorker *);
void genesis_worker_destroy(GenesisWorker *);
uint64_t genesis_worker_completed(const GenesisWorker *);
#endif

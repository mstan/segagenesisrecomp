/* One persistent host worker. Inputs remain owned until completion/join.
 * Simulation jobs join at their dependency boundary; host-only media work may
 * span ticks while its owner retains the immutable inputs. No SDL renderer work. */
#ifndef GENESIS_ENGINE_WORKER_H
#define GENESIS_ENGINE_WORKER_H
#include <stdint.h>
typedef struct GenesisWorker GenesisWorker;
typedef void (*GenesisWorkFunction)(void *);
GenesisWorker *genesis_worker_create(void);
/* NULL worker executes synchronously. A non-NULL worker first joins any prior
 * job, publishes this one exactly once, then returns without waiting. */
int genesis_worker_start(GenesisWorker *,GenesisWorkFunction,void *);
/* Nonblocking admission: busy/NULL does no work and never executes inline.
 * busy()==0 synchronizes completed job writes with the owner. */
int genesis_worker_try_start(GenesisWorker *,GenesisWorkFunction,void *);
int genesis_worker_busy(GenesisWorker *);
void genesis_worker_join(GenesisWorker *);
void genesis_worker_destroy(GenesisWorker *);
uint64_t genesis_worker_completed(const GenesisWorker *);
#endif

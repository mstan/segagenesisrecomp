#ifndef GENESIS_PERSIST_WRITER_H
#define GENESIS_PERSIST_WRITER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct GenesisPersistWriter GenesisPersistWriter;
typedef int (*GenesisPersistWrite)(const char *, const void *, size_t);
typedef void (*GenesisPersistCompleted)(const char *, const void *, size_t, void *);
/* The callback writes immutable fixed-size bytes and reports durable/atomic
 * completion. The caller retains its live state; path and bytes are copied. */
GenesisPersistWriter *genesis_persist_create(const char *, const void *, size_t,
                                             GenesisPersistWrite);
/* Nonblocking health check: 1 healthy (including busy), 0 failed. The internal
 * committed comparison advances only after a completed successful callback. */
int genesis_persist_poll(GenesisPersistWriter *);
/* Configure on the producer before submission. Notifications run only on the
 * producer during poll/wait, after durable success, with committed snapshot
 * bytes borrowed for the duration of the callback. Never run on the worker. */
void genesis_persist_on_completed(GenesisPersistWriter *, GenesisPersistCompleted, void *);
/* 1 accepted/already committed, 0 prior snapshot still busy, -1 write failed.
 * Acceptance does not acknowledge persistence; flush waits for completion. */
int genesis_persist_submit(GenesisPersistWriter *, const void *);
/* Preserve callers that require an actual replacement even for equal bytes. */
int genesis_persist_submit_force(GenesisPersistWriter *, const void *);
/* Drain only the accepted snapshot; do not publish later mutable live data. */
int genesis_persist_wait(GenesisPersistWriter *);
int genesis_persist_flush(GenesisPersistWriter *, const void *latest);
/* Drains an accepted job; does not manufacture an unsubmitted snapshot. */
int genesis_persist_destroy(GenesisPersistWriter *);
#ifdef __cplusplus
}
#endif
#endif

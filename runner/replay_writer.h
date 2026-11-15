/* Adapted from snesrecomp runner/src/diagnostics/replay_writer (Matthew
 * Stanley, PolyForm Noncommercial 1.0.0). One bounded producer and disk worker.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* One simulation producer, one disk worker. Frame hashes completed state
 * in place on the caller and queues only its immutable text record. Other
 * records/attachments copy into the same fixed 16 MiB queue. The worker owns
 * files; a slow sink stops capture without waiting for disk capacity.
 * Destroy waits only at shutdown or an explicit playback transition. */
typedef struct GenesisReplayWriter GenesisReplayWriter;
GenesisReplayWriter *GenesisReplayWriterCreate(void);
bool GenesisReplayWriterBegin(GenesisReplayWriter *, const char *path,
                              const char *header, const char *config_text,
                              const char *mods, const char *effective);
bool GenesisReplayWriterSnapshot(GenesisReplayWriter *, const void *, size_t);
bool GenesisReplayWriterLine(GenesisReplayWriter *, const char *);
/* Completed H partitions; the queue owns the formatted record on return. */
bool GenesisReplayWriterHashes(GenesisReplayWriter *, unsigned frame,
                                const uint32_t hashes[4]);
bool GenesisReplayWriterFrame(GenesisReplayWriter *, unsigned frame,
                              const void *wram, size_t nw, const void *coop,
                              size_t nc, const void *views, size_t nv,
                              const void *vram, size_t np);
bool GenesisReplayWriterRemove(GenesisReplayWriter *, const char *path);
bool GenesisReplayWriterEnd(GenesisReplayWriter *);
const char *GenesisReplayWriterError(GenesisReplayWriter *);
/* Only at shutdown or an explicit transition to offline playback. */
const char *GenesisReplayWriterDestroy(GenesisReplayWriter *);
#ifdef __cplusplus
}
#endif

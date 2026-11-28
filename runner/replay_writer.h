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

/* One simulation producer, one disk worker. All calls except Destroy only
 * copy into a fixed 16 MiB queue; a slow sink stops capture, never waits.
 * The worker owns files and hashes immutable copies of completed frames. */
typedef struct GenesisReplayWriter GenesisReplayWriter;
GenesisReplayWriter *GenesisReplayWriterCreate(void);
bool GenesisReplayWriterBegin(GenesisReplayWriter *, const char *path,
                              const char *header, const char *config_text,
                              const char *mods, const char *effective);
bool GenesisReplayWriterSnapshot(GenesisReplayWriter *, const void *, size_t);
bool GenesisReplayWriterLine(GenesisReplayWriter *, const char *);
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

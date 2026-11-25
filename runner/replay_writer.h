#pragma once
#include <recomp_diagnostics/replay_c.h>
typedef RecompReplayWriter GenesisReplayWriter;
#define GenesisReplayWriterCreate recomp_replay_create
#define GenesisReplayWriterBegin recomp_replay_begin
#define GenesisReplayWriterSnapshot recomp_replay_snapshot
#define GenesisReplayWriterLine recomp_replay_line
#define GenesisReplayWriterHashes recomp_replay_hashes
#define GenesisReplayWriterRemove recomp_replay_remove
#define GenesisReplayWriterEnd recomp_replay_end
#define GenesisReplayWriterError recomp_replay_error
#define GenesisReplayWriterDestroy recomp_replay_destroy
#ifdef __cplusplus
extern "C" {
#endif
bool GenesisReplayWriterFrame(GenesisReplayWriter *,unsigned,const void *,size_t,const void *,size_t,const void *,size_t,const void *,size_t);
#ifdef __cplusplus
}
#endif

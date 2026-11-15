#ifndef GENESIS_REPLAY_HASH_H
#define GENESIS_REPLAY_HASH_H
#include <stddef.h>
#include <stdint.h>

/* A build-specific checksum over every checkpoint byte. Use the existing
 * portable XXH64 implementation for recording and playback, then fold to the
 * recording format's 32-bit field. Only the completed hashes cross the queue. */
#define XXH_INLINE_ALL
#define XXH_NO_XXH3
#include "external/xxhash/xxhash.h"
static inline uint32_t genesis_replay_hash_fold(uint64_t hash)
{ return (uint32_t)(hash ^ (hash >> 32)); }
static inline uint32_t genesis_replay_hash(const void *data,size_t size)
{ return genesis_replay_hash_fold(XXH64(data,size,0)); }
typedef XXH64_state_t GenesisReplayHash;
static inline void genesis_replay_hash_init(GenesisReplayHash *state)
{ XXH64_reset(state,0); }
static inline int genesis_replay_hash_update(GenesisReplayHash *state,const void *data,size_t size)
{ return !size || (data && XXH64_update(state,data,size)==XXH_OK); }
static inline uint32_t genesis_replay_hash_finish(const GenesisReplayHash *state)
{ return genesis_replay_hash_fold(XXH64_digest(state)); }
#endif

#include "replay_writer.h"
#include "replay_hash.h"
bool GenesisReplayWriterFrame(GenesisReplayWriter *w, unsigned frame,
                              const void *a, size_t na, const void *b,
                              size_t nb, const void *c, size_t nc,
                              const void *d, size_t nd) {
  if (!w || GenesisReplayWriterError(w))
    return false;
  if ((na && !a) || (nb && !b) || (nc && !c) || (nd && !d) ||
      na >= (16u*1024u*1024u) || nb >= (16u*1024u*1024u) || nc >= (16u*1024u*1024u) ||
      nd >= (16u*1024u*1024u) || na + nb + nc + nd >= (16u*1024u*1024u-7u*sizeof(uint32_t))) {
    GenesisReplayWriterLine(w,NULL);
    return false;
  }
  /* These bytes already belong to the simulation producer. Hash them once
   * in place and queue only their immutable record, not another full frame
   * for the disk thread to copy and reread. File I/O remains asynchronous. */
  uint32_t hashes[4]={genesis_replay_hash(a,na),genesis_replay_hash(b,nb),
                      genesis_replay_hash(c,nc),genesis_replay_hash(d,nd)};
  return GenesisReplayWriterHashes(w,frame,hashes);
}

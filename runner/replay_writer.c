/* Adapted from snesrecomp runner/src/diagnostics/replay_writer (Matthew
 * Stanley, PolyForm Noncommercial 1.0.0). One bounded producer and disk worker.
 */
#include "replay_writer.h"
#include "replay_hash.h"
#include <SDL2/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { QUEUE_BYTES = 16 * 1024 * 1024, FILE_BUFFER_BYTES = 65536,
       FRAME_BYTES_LIMIT = QUEUE_BYTES - 7 * sizeof(uint32_t) };
enum { BEGIN, SNAPSHOT, LINE, REMOVE, END };
enum { OK, FULL, INVALID, IO };
typedef struct Command {
  uint32_t op, size;
} Command;
typedef struct Begin {
  char path[1280], header[512], config[1200], mods[1200], effective[16384];
} Begin;
struct GenesisReplayWriter {
  SDL_Thread *thread;
  SDL_atomic_t stop, error, read_pos, write_pos;
  unsigned head; /* Producer only. */
  unsigned char bytes[QUEUE_BYTES];
};
static void fail(GenesisReplayWriter *w, int error) {
  SDL_AtomicCAS(&w->error, OK, error);
}
static void put(GenesisReplayWriter *w, unsigned at, const void *data,
                size_t n) {
  if (!n)
    return;
  size_t first = QUEUE_BYTES - at;
  if (first > n)
    first = n;
  if (first)
    memcpy(w->bytes + at, data, first);
  if (n > first)
    memcpy(w->bytes, (const char *)data + first, n - first);
}
static void get(GenesisReplayWriter *w, unsigned at, void *data, size_t n) {
  size_t first = QUEUE_BYTES - at;
  if (first > n)
    first = n;
  if (first)
    memcpy(data, w->bytes + at, first);
  if (n > first)
    memcpy((char *)data + first, w->bytes, n - first);
}
static bool enqueue(GenesisReplayWriter *w, Command c, const void *data) {
  if (!w || SDL_AtomicGet(&w->stop) || SDL_AtomicGet(&w->error))
    return false;
  unsigned tail = (unsigned)SDL_AtomicGet(&w->read_pos);
  if (sizeof(c) + c.size >= (size_t)QUEUE_BYTES) {
    fail(w, INVALID);
    return false;
  }
  if (c.size && !data) {
    fail(w, INVALID);
    return false;
  }
  if (sizeof(c) + c.size > (tail + QUEUE_BYTES - w->head - 1) % QUEUE_BYTES) {
    fail(w, FULL);
    return false;
  }
  unsigned at = w->head;
  put(w, at, &c, sizeof(c));
  at = (at + sizeof(c)) % QUEUE_BYTES;
  put(w, at, data, c.size);
  at = (at + c.size) % QUEUE_BYTES;
  w->head = at;
  /* SDL atomics publish all payload bytes before the consumer sees the head. */
  SDL_AtomicSet(&w->write_pos, (int)at);
  return true;
}
static bool one(GenesisReplayWriter *w, unsigned op, const void *data,
                size_t n) {
  if (n >= QUEUE_BYTES) {
    if (w)
      fail(w, INVALID);
    return false;
  }
  Command c = {op, (uint32_t)n};
  return enqueue(w, c, data);
}
/* State attachments retain the offline inspector's existing checksum. Only
 * per-frame hashes use the faster build-specific checksum on the producer. */
static uint32_t snapshot_digest(GenesisReplayWriter *w,unsigned at,size_t n) {
  uint32_t hash=2166136261u;
  while(n) {
    size_t count=QUEUE_BYTES-at;
    if(count>n)count=n;
    for(size_t i=0;i<count;++i)hash=(hash^w->bytes[at+i])*16777619u;
    n-=count;at=0;
  }
  return hash;
}
static bool write_ring(GenesisReplayWriter *w, FILE *f, unsigned at, size_t n) {
  size_t first = QUEUE_BYTES - at;
  if (first > n)
    first = n;
  return f && fwrite(w->bytes + at, 1, first, f) == first &&
         (n == first || fwrite(w->bytes, 1, n - first, f) == n - first);
}
static bool close_files(FILE **text, FILE **states) {
  bool ok = true;
  if (*states && fclose(*states))
    ok = false;
  if (*text && fclose(*text))
    ok = false;
  *text = *states = NULL;
  return ok;
}
static bool copy_optional(const char *source, const char *dest) {
  if (!source || !*source)
    return true;
  FILE *in = fopen(source, "rb");
  if (!in)
    return errno == ENOENT;
  FILE *out = fopen(dest, "wb");
  bool ok = out != NULL;
  char bytes[8192];
  size_t n;
  while (ok && (n = fread(bytes, 1, sizeof(bytes), in)))
    ok = fwrite(bytes, 1, n, out) == n;
  if (ferror(in))
    ok = false;
  if (fclose(in))
    ok = false;
  if (out && fclose(out))
    ok = false;
  return ok;
}
/* Three exact, engine-owned filenames per game; never scan or delete other
 * logs. A foreign file at one of these names stops recording safely. */
static bool rotate_history(const char *base) {
  const char *suffix[] = {"", ".sav", ".config.ini", ".mods.toml", ".mods.txt"};
  char src[1500], dst[1500], row[64];
  for (int i = 2; i >= 0; --i) {
    if (i)
      snprintf(src, sizeof src, "%s.previous-%d", base, i);
    else
      snprintf(src, sizeof src, "%s", base);
    FILE *f = fopen(src, "rb");
    if (!f) {
      if (errno != ENOENT)
        return false;
      continue;
    }
    bool owned = fgets(row, sizeof row, f) && !strcmp(row, "GENESISREPLAY 1\n");
    fclose(f);
    if (!owned)
      return false;
    for (unsigned j = 0; j < sizeof suffix / sizeof suffix[0]; ++j) {
      if (i)
        snprintf(src, sizeof src, "%s.previous-%d%s", base, i, suffix[j]);
      else
        snprintf(src, sizeof src, "%s%s", base, suffix[j]);
      if (i == 2) {
        if (remove(src) && errno != ENOENT)
          return false;
      } else {
        snprintf(dst, sizeof dst, "%s.previous-%d%s", base, i + 1, suffix[j]);
        if (rename(src, dst) && errno != ENOENT)
          return false;
      }
    }
  }
  return true;
}
static int SDLCALL worker(void *opaque) {
  GenesisReplayWriter *w = opaque;
  FILE *text = NULL, *states = NULL;
  char text_buffer[FILE_BUFFER_BYTES], state_buffer[FILE_BUFFER_BYTES];
  unsigned tail = 0;
  size_t offset = 0;
  uint32_t last_flush = (uint32_t)SDL_GetTicks();
  bool dirty = false;
#ifdef GENESIS_REPLAY_WRITER_TEST
  extern void GenesisReplayWriterTestGate(void);
  GenesisReplayWriterTestGate();
#endif
  for (;;) {
    if ((unsigned)SDL_AtomicGet(&w->write_pos) != tail) {
      Command c;
      get(w, tail, &c, sizeof(c));
      unsigned at = (tail + sizeof(c)) % QUEUE_BYTES;
      bool ok = true;
      switch (c.op) {
      case BEGIN: {
        Begin b;
        get(w, at, &b, sizeof(b));
        char path[1400];
        ok = close_files(&text, &states);
        offset = 0;
        if (!ok)
          break;
        if (!rotate_history(b.path)) {
          ok = false;
          break;
        }
        text = fopen(b.path, "wb");
        snprintf(path, sizeof(path), "%s.sav", b.path);
        states = fopen(path, "wb");
        ok = text && states &&
             !setvbuf(text, text_buffer, _IOFBF, sizeof(text_buffer)) &&
             !setvbuf(states, state_buffer, _IOFBF, sizeof(state_buffer));
        if (!ok)
          break;
        ok = fputs(b.header, text) != EOF;
        snprintf(path, sizeof(path), "%s.config.ini", b.path);
        FILE *config = fopen(path, "wb");
        if (!config)
          ok = false;
        else {
          if (fputs(b.config, config) == EOF)
            ok = false;
          if (fclose(config))
            ok = false;
        }
        snprintf(path, sizeof(path), "%s.mods.toml", b.path);
        ok = copy_optional(b.mods, path) && ok;
        snprintf(path, sizeof(path), "%s.mods.txt", b.path);
        FILE *selected = fopen(path, "wb");
        if (!selected)
          ok = false;
        else {
          if (fputs(b.effective, selected) == EOF)
            ok = false;
          if (fclose(selected))
            ok = false;
        }
        break;
      }
      case SNAPSHOT:
        ok = write_ring(w, states, at, c.size);
        if (ok)
          ok = text && fprintf(text, "S %zu %u %08x\n", offset, c.size,
                               snapshot_digest(w, at, c.size)) >= 0;
        offset += c.size;
        break;
      case LINE:
        ok = write_ring(w, text, at, c.size);
        break;
      case REMOVE: {
        char path[1400];
        get(w, at, path, c.size);
        ok = !remove(path) || errno == ENOENT;
        break;
      }
      case END:
        ok = close_files(&text, &states);
        break;
      default:
        ok = false;
        break;
      }
      if (!ok) {
        fail(w, IO);
        break;
      }
      /* Keep the payload owned until disk I/O and hashing finish. The producer
       * stops at capacity instead of waiting for this publication. */
      tail = (tail + sizeof(c) + c.size) % QUEUE_BYTES;
      SDL_AtomicSet(&w->read_pos, (int)tail);
      dirty = text != NULL;
    } else {
      if (SDL_AtomicGet(&w->stop) || SDL_AtomicGet(&w->error)) {
        if ((unsigned)SDL_AtomicGet(&w->write_pos) == tail)
          break;
        continue;
      }
      SDL_Delay(2);
    }
    uint32_t now = (uint32_t)SDL_GetTicks();
    if (dirty && now - last_flush >= 100) {
      /* Sidecars reach stdio's sink before records that refer to them. A
       * hung simulation still leaves its last queued input in this flush. */
      if (fflush(states) || fflush(text)) {
        fail(w, IO);
        break;
      }
      last_flush = now;
      dirty = false;
    }
  }
  if (!close_files(&text, &states))
    fail(w, IO);
  return SDL_AtomicGet(&w->error) != OK;
}
GenesisReplayWriter *GenesisReplayWriterCreate(void) {
  GenesisReplayWriter *w = calloc(1, sizeof(*w));
  if (!w)
    return NULL;
  w->thread = SDL_CreateThread(worker, "genesis-replay-writer", w);
  if (!w->thread) {
    free(w);
    return NULL;
  }
  return w;
}
bool GenesisReplayWriterBegin(GenesisReplayWriter *w, const char *path,
                              const char *header, const char *config,
                              const char *mods, const char *effective) {
  Begin b = {0};
#define FIELD(name)                                                            \
  if (!name || strlen(name) >= sizeof(b.name)) {                               \
    if (w)                                                                     \
      fail(w, INVALID);                                                        \
    return false;                                                              \
  } else                                                                       \
    strcpy(b.name, name)
  FIELD(path);
  FIELD(header);
  FIELD(config);
  FIELD(mods);
  FIELD(effective);
#undef FIELD
  return one(w, BEGIN, &b, sizeof(b));
}
bool GenesisReplayWriterSnapshot(GenesisReplayWriter *w, const void *p,
                                 size_t n) {
  return one(w, SNAPSHOT, p, n);
}
bool GenesisReplayWriterLine(GenesisReplayWriter *w, const char *line) {
  return line && one(w, LINE, line, strlen(line));
}
bool GenesisReplayWriterHashes(GenesisReplayWriter *w,unsigned frame,const uint32_t hashes[4]) {
  if (!w || SDL_AtomicGet(&w->stop) || SDL_AtomicGet(&w->error))return false;
  if (!hashes) { fail(w,INVALID);return false; }
  char row[64];
  int n=snprintf(row,sizeof row,"H %u %08x %08x %08x %08x\n",frame,
                 hashes[0],hashes[1],hashes[2],hashes[3]);
  return n>0 && (size_t)n<sizeof row && one(w,LINE,row,(size_t)n);
}
bool GenesisReplayWriterFrame(GenesisReplayWriter *w, unsigned frame,
                              const void *a, size_t na, const void *b,
                              size_t nb, const void *c, size_t nc,
                              const void *d, size_t nd) {
  if (!w || SDL_AtomicGet(&w->stop) || SDL_AtomicGet(&w->error))
    return false;
  if ((na && !a) || (nb && !b) || (nc && !c) || (nd && !d) ||
      na >= QUEUE_BYTES || nb >= QUEUE_BYTES || nc >= QUEUE_BYTES ||
      nd >= QUEUE_BYTES || na + nb + nc + nd >= FRAME_BYTES_LIMIT) {
    fail(w, INVALID);
    return false;
  }
  /* These bytes already belong to the simulation producer. Hash them once
   * in place and queue only their immutable record, not another full frame
   * for the disk thread to copy and reread. File I/O remains asynchronous. */
  uint32_t hashes[4]={genesis_replay_hash(a,na),genesis_replay_hash(b,nb),
                      genesis_replay_hash(c,nc),genesis_replay_hash(d,nd)};
  return GenesisReplayWriterHashes(w,frame,hashes);
}
bool GenesisReplayWriterRemove(GenesisReplayWriter *w, const char *path) {
  if (!path || strlen(path) >= 1400) {
    if (w)
      fail(w, INVALID);
    return false;
  }
  return one(w, REMOVE, path, strlen(path) + 1);
}
bool GenesisReplayWriterEnd(GenesisReplayWriter *w) {
  return one(w, END, "", 0);
}
const char *GenesisReplayWriterError(GenesisReplayWriter *w) {
  switch (w ? SDL_AtomicGet(&w->error) : OK) {
  case FULL:
    return "replay disk writer fell behind; bounded queue full";
  case INVALID:
    return "invalid or oversized replay write";
  case IO:
    return "replay file write failed";
  default:
    return NULL;
  }
}
const char *GenesisReplayWriterDestroy(GenesisReplayWriter *w) {
  if (!w)
    return NULL;
  SDL_AtomicSet(&w->stop, 1);
  SDL_WaitThread(w->thread, NULL);
  const char *error = GenesisReplayWriterError(w);
  free(w);
  return error;
}

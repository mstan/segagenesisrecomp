/* Boot-rooted feedback recordings. Native fiber snapshots are process-local:
 * K saves a checkpoint in execution order; R restores that checkpoint in the
 * reproducing process. No captured host address is ever loaded from disk. */
#include "diagnostic_replay.h"
#include "app_config.h"
#include "game_spec.h"
#include "rb_state.h"
#include "replay_writer.h"
#include "runtime_evidence.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#endif

enum {
  MAX_TEXT = 64 * 1024 * 1024,
  MAX_STATES = 32 * 1024 * 1024,
  MAX_SNAPSHOT = 8 * 1024 * 1024,
  MAX_KEYS = 1000000,
  MAX_CACHE = 256 * 1024 * 1024
};
typedef struct ReplayKey {
  void *blob,*presentation;
  size_t size,presentation_size;
  unsigned uses;
} ReplayKey;
static GenesisReplayWriter *writer;
static FILE *stream, *states;
static int active, playing, failed, finished, pending, initialized;
static char path[1280], folder[1024], effective[16384];
static size_t text_bytes, state_bytes, cache_bytes;
static unsigned executions;
static uint64_t key_id;
static ReplayKey *keys;
static unsigned key_count;
static void *scratch;
static size_t scratch_cap;
static GenesisReplayViewFn view_fn;
static GenesisReplayView playback_view;
int runner_load_state_file(const char *);
int runner_save_state_file(const char *);
void runner_replay_sram_flushed(void);

static uint32_t hash(const void *data, size_t n) {
  const unsigned char *p = data;
  uint32_t h = 2166136261u;
  while (n--)
    h = (h ^ *p++) * 16777619u;
  return h;
}
static int bounded_file(FILE *f, long limit) {
  if (!f || fseek(f, 0, SEEK_END))
    return 0;
  long n = ftell(f);
  return n >= 0 && n <= limit && !fseek(f, 0, SEEK_SET);
}
static void fail(const char *why) {
  if (!failed)
    fprintf(stderr, "[feedback-replay] %s: %s\n", path,
            why ? why : "recording failed");
  failed = 1;
  active = 0;
  pending = 0;
}
static int line(const char *s) {
  if (!active)
    return 0;
  size_t n = strlen(s);
  if (n > MAX_TEXT - text_bytes) {
    fail("64 MiB input history limit reached; restart the game for another "
         "report");
    return 0;
  }
  text_bytes += n;
  if (!GenesisReplayWriterLine(writer, s)) {
    fail(GenesisReplayWriterError(writer));
    return 0;
  }
  return 1;
}
static int snapshot(const void *data, size_t n) {
  if (MAX_TEXT - text_bytes < 64) {
    fail("input history limit reached");
    return 0;
  }
  text_bytes += 64; /* The worker's S offset/length/hash record. */
  if (n > MAX_SNAPSHOT || n > MAX_STATES - state_bytes) {
    fail("bounded state attachment limit reached");
    return 0;
  }
  state_bytes += n;
  if (!GenesisReplayWriterSnapshot(writer, data, n)) {
    fail(GenesisReplayWriterError(writer));
    return 0;
  }
  return 1;
}
int genesis_replay_playing(void) { return playing; }
int genesis_replay_failed(void) { return failed; }
int genesis_replay_finished(void) { return finished; }
const char *genesis_replay_path(void) { return path; }
void genesis_replay_viewport(int *w, int *h) {
  if (playing && pending) {
    *w = playback_view.drawable_w;
    *h = playback_view.drawable_h;
  }
}

/* Effective provider values capture lobby overrides as well as offline mods.
 * Values are tab-separated; reject ambiguous text rather than misreplay it. */
#if RECOMP_LAUNCHER
static int clean(const char *s) { return s && !strpbrk(s, "\t\r\n"); }
#endif
int genesis_replay_mods(const struct RecompLauncherCModProvider *p,
                        const char *playback) {
  effective[0] = 0;
#if RECOMP_LAUNCHER
  if (playback) {
    /* Providers also need the playback policy while restoring resources. */
    playing = 1;
    char name[1400], row[2048];
    snprintf(name, sizeof name, "%s.mods.txt", playback);
    FILE *f = fopen(name, "rb");
    if (!f)
      return 0;
    int ok = 1;
    /* Disable first: character providers can enforce mutual exclusion. */
    if (p && p->feature_count && p->feature_get && p->feature_enable)
      for (int i = 0; i < p->feature_count(p->ctx); ++i) {
        RecompLauncherCModFeature feature;
        if (!p->feature_get(p->ctx, i, &feature) ||
            !p->feature_enable(p->ctx, feature.package_id, feature.id, 0)) {
          ok = 0;
          break;
        }
      }
    while (ok && fgets(row, sizeof row, f)) {
      row[strcspn(row, "\r\n")] = 0;
      if (!strcmp(row, "(none)"))
        continue;
      char *kind = strtok(row, "\t"), *package = strtok(NULL, "\t"),
           *feature = strtok(NULL, "\t"), *option = strtok(NULL, "\t"),
           *value = strtok(NULL, "\t");
      if (!p || !kind || !package || !feature || !option ||
          strtok(NULL, "\t")) {
        ok = 0;
        break;
      }
      if (!strcmp(kind, "F"))
        ok = !value && !strcmp(option, "1") && p->feature_enable &&
             p->feature_enable(p->ctx, package, feature, 1);
      else if (!strcmp(kind, "O"))
        ok = value && p->feature_set_option &&
             p->feature_set_option(p->ctx, package, feature, option, value);
      else if (!strcmp(kind, "P"))
        ok = value && p->feature_resource_set_path &&
             p->feature_resource_set_path(p->ctx, package, feature, option,
                                          value);
      else
        ok = 0;
    }
    if (ferror(f))
      ok = 0;
    fclose(f);
    return ok;
  }
  size_t at = 0;
  if (p && p->feature_count && p->feature_get)
    for (int i = 0; i < p->feature_count(p->ctx); ++i) {
      RecompLauncherCModFeature f;
      if (!p->feature_get(p->ctx, i, &f))
        return 0;
      if (!f.enabled)
        continue;
      if (!clean(f.package_id) || !clean(f.id))
        return 0;
      int n;
      int resources =
          p->feature_resource_count
              ? p->feature_resource_count(p->ctx, f.package_id, f.id)
              : 0;
      for (int j = 0; j < resources; ++j) {
        RecompLauncherCModResource resource;
        if (!p->feature_resource_get ||
            !p->feature_resource_get(p->ctx, f.package_id, f.id, j,
                                     &resource) ||
            !clean(resource.id) || !clean(resource.path))
          return 0;
        if (!*resource.path)
          continue;
        n = snprintf(effective + at, sizeof effective - at,
                     "P\t%s\t%s\t%s\t%s\n", f.package_id, f.id, resource.id,
                     resource.path);
        if (n < 0 || (size_t)n >= sizeof effective - at)
          return 0;
        at += (size_t)n;
      }
      for (int j = 0; j < f.option_count; ++j) {
        RecompLauncherCModOption o;
        if (!p->feature_option_get ||
            !p->feature_option_get(p->ctx, f.package_id, f.id, j, &o) ||
            !clean(o.id) || !clean(o.value) || !*o.value)
          return 0;
        n = snprintf(effective + at, sizeof effective - at,
                     "O\t%s\t%s\t%s\t%s\n", f.package_id, f.id, o.id, o.value);
        if (n < 0 || (size_t)n >= sizeof effective - at)
          return 0;
        at += (size_t)n;
      }
      /* Source resources and option values must be in place before enabling
       * a feature whose provider validates them at activation. */
      n = snprintf(effective + at, sizeof effective - at, "F\t%s\t%s\t1\n",
                   f.package_id, f.id);
      if (n < 0 || (size_t)n >= sizeof effective - at)
        return 0;
      at += (size_t)n;
    }
#else
  (void)p;
  if (playback) {
    char name[1400], row[32];
    snprintf(name, sizeof name, "%s.mods.txt", playback);
    FILE *f = fopen(name, "rb");
    if (!f)
      return 0;
    int ok = fgets(row, sizeof row, f) && !strcmp(row, "(none)\n") &&
             fgetc(f) == EOF;
    fclose(f);
    return ok;
  }
#endif
  if (!*effective)
    strcpy(effective, "(none)\n");
  return 1;
}

static int read_snapshot(void **data, size_t *size) {
  char row[256], extra;
  unsigned n, digest;
  unsigned long long offset;
  if (!fgets(row, sizeof row, stream) ||
      sscanf(row, "S %llu %u %x %c", &offset, &n, &digest, &extra) != 3 ||
      n > MAX_SNAPSHOT || offset > MAX_STATES || n > MAX_STATES - offset)
    return 0;
  void *b = malloc(n ? n : 1);
  if (!b)
    return 0;
  if (fseek(states, (long)offset, SEEK_SET) || fread(b, 1, n, states) != n ||
      hash(b, n) != digest) {
    free(b);
    return 0;
  }
  *data = b;
  *size = n;
  return 1;
}
static int scan_keys(void) {
  char row[1024];
  long start = ftell(stream);
  unsigned long long id;
  unsigned max = 0;
  while (fgets(row, sizeof row, stream))
    if (sscanf(row, "K %llu", &id) == 1) {
      if (id != max + 1 || id > MAX_KEYS)
        return 0;
      max = (unsigned)id;
    }
  if (ferror(stream) || fseek(stream, start, SEEK_SET))
    return 0;
  keys = calloc((size_t)max + 1, sizeof *keys);
  if (!keys)
    return 0;
  key_count = max;
  while (fgets(row, sizeof row, stream))
    if (sscanf(row, "R %llu", &id) == 1) {
      if (!id || id > max || keys[id].uses == ~0u)
        return 0;
      ++keys[id].uses;
    }
  return !ferror(stream) && !fseek(stream, start, SEEK_SET);
}
int genesis_replay_boot_sram(const char *name, void *sram, size_t size,
                             int *loaded) {
  char sidecar[1400], row[512];
  unsigned flag = 2, exe = 0;
  FILE *f = fopen(name, "rb");
  snprintf(sidecar, sizeof sidecar, "%s.sav", name);
  FILE *s = fopen(sidecar, "rb");
  int ok = bounded_file(f, MAX_TEXT) && bounded_file(s, MAX_STATES) &&
           fgets(row, sizeof row, f) && !strcmp(row, "GENESISREPLAY 1\n") &&
           fgets(row, sizeof row, f) && sscanf(row, "BUILD %x", &exe) == 1 &&
           exe == genesis_build_exe_fingerprint() &&
           fgets(row, sizeof row, f) && fgets(row, sizeof row, f) &&
           sscanf(row, "SRAM %u", &flag) == 1 && flag <= 1;
  FILE *old_stream = stream, *old_states = states;
  stream = f;
  states = s;
  void *boot = NULL;
  size_t n = 0;
  if (ok)
    ok = read_snapshot(&boot, &n) && n == size && (!n || sram);
  if (ok) {
    if (n)
      memcpy(sram, boot, n);
    *loaded = (int)flag;
  }
  free(boot);
  stream = old_stream;
  states = old_states;
  if (f)
    fclose(f);
  if (s)
    fclose(s);
  return ok;
}

int genesis_replay_init(const char *directory, const char *playback,
                        uint32_t rom, size_t rom_n, void *sram, size_t sram_n,
                        int sram_loaded, const char *settings,
                        GenesisReplayViewFn fn) {
  view_fn = fn;
  initialized = 1;
  snprintf(folder, sizeof folder, "%s", directory);
  if (playback) {
    playing = 1;
    snprintf(path, sizeof path, "%s", playback);
    char sidecar[1400], row[512], extra;
    unsigned format, exe, crc, loaded;
    size_t bytes;
    stream = fopen(path, "rb");
    snprintf(sidecar, sizeof sidecar, "%s.sav", path);
    states = fopen(sidecar, "rb");
    if (!bounded_file(stream, MAX_TEXT) || !bounded_file(states, MAX_STATES) ||
        !fgets(row, sizeof row, stream) ||
        sscanf(row, "GENESISREPLAY %u %c", &format, &extra) != 1 ||
        format != 1 || !fgets(row, sizeof row, stream) ||
        sscanf(row, "BUILD %x", &exe) != 1 ||
        exe != genesis_build_exe_fingerprint() ||
        !fgets(row, sizeof row, stream) ||
        sscanf(row, "ROM %x %zu %c", &crc, &bytes, &extra) != 2 || crc != rom ||
        bytes != rom_n || !fgets(row, sizeof row, stream) ||
        sscanf(row, "SRAM %u %c", &loaded, &extra) != 1 || loaded > 1) {
      fail("missing, damaged, wrong-ROM or different-build report");
      return 0;
    }
    void *boot = NULL;
    size_t n = 0;
    if (!read_snapshot(&boot, &n) || n != sram_n || (n && !sram)) {
      free(boot);
      fail("invalid startup SRAM attachment");
      return 0;
    }
    /* The desktop applied the boot image before its SRAM-loaded callback.
     * Reapplying here would undo legitimate changes made by that callback. */
    free(boot);
    if (g_game_spec.feedback_context) {
      void *context=NULL;size_t context_n=0;
      int ok=fgets(row,sizeof row,stream) && !strcmp(row,"A\n") &&
          read_snapshot(&context,&context_n) && context_n &&
          g_game_spec.feedback_context(context,context_n,1)==context_n;
      free(context);
      if(!ok){fail("invalid or unavailable startup save context");return 0;}
    }
    if (!scan_keys()) {
      fail("invalid checkpoint history");
      return 0;
    }
    return 1;
  }
  const char *record = getenv("GENESIS_REPLAY_RECORD");
  if (!g_app_config.replay_diagnostics || (record && !strcmp(record, "0")))
    return 1;
  char logs[1200];
  snprintf(logs, sizeof logs, "%s/logs", folder);
  if (make_dir(logs) && errno != EEXIST) {
    fail("cannot create logs directory");
    return 1;
  }
  snprintf(path, sizeof path, "%s/%s-feedback.replay", logs,
           g_game_spec.short_name ? g_game_spec.short_name : "game");
  writer = GenesisReplayWriterCreate();
  if (!writer) {
    fail("cannot start background writer");
    return 1;
  }
  char header[512];
  snprintf(header, sizeof header,
           "GENESISREPLAY 1\nBUILD %08x %s\nROM %08x %zu\nSRAM %d\n",
           genesis_build_exe_fingerprint(), genesis_build_info_string(), rom,
           rom_n, sram_loaded != 0);
  /* Effective mods are authoritative; Genesis has no separate package TOML. */
  char config[1200];
  snprintf(config, sizeof config,
           "[video]\nwindow_scale = %d\nfullscreen = 0\nlinear_filter = "
           "%d\nwidescreen = %d\nwidescreen_cells = %d\npresentation_rate = %d\n"
           "[audio]\nvolume = %d\nbypass_filter = %d\n[launcher]\nskip_launcher = "
           "1\n[rewind]\nenabled = 0\n"
           "[mods.widescreen]\nenabled = %d\naspect = "
           "%s\n[mods.diagnostics]\nenabled = 0\n",
           g_app_config.window_scale, g_app_config.linear_filter,
           g_app_config.widescreen, g_app_config.widescreen_cells, g_app_config.presentation_rate,
           g_app_config.volume, g_app_config.audio_filter_bypass, g_app_config.custom_widescreen,
           app_config_aspect_mode(g_app_config.custom_aspect));
  if (!GenesisReplayWriterBegin(writer, path, header, config, "", effective)) {
    fail(GenesisReplayWriterError(writer));
    return 1;
  }
  (void)settings;
  active = 1;
  text_bytes = strlen(header);
  snapshot(sram, sram_n);
  if (g_game_spec.feedback_context) {
    size_t n=g_game_spec.feedback_context(NULL,0,0);
    void *context=n && n<=MAX_SNAPSHOT?calloc(1,n):NULL;
    if(!context || g_game_spec.feedback_context(context,n,0)!=n)
      fail("cannot capture startup save context");
    else if(line("A\n"))snapshot(context,n);
    free(context);
  }
  fprintf(stderr,
          "[feedback-replay] %s (default on; attach replay and all sidecars)\n",
          path);
  return 1;
}

uint64_t genesis_replay_snapshot_saved(void) {
  if (!active)
    return 0;
  if (pending) {
    fail("snapshot taken inside a tick; recording cannot reproduce it");
    return 0;
  }
  if (key_id >= MAX_KEYS) {
    fail("checkpoint history limit reached");
    return 0;
  }
  char row[64];
  snprintf(row, sizeof row, "K %llu\n", (unsigned long long)++key_id);
  line(row);
  return key_id;
}
void genesis_replay_snapshot_loaded(uint64_t id) {
  if (!active)
    return;
  if (pending || !id || id > key_id) {
    fail("restored checkpoint was not recorded");
    return;
  }
  char row[64];
  snprintf(row, sizeof row, "R %llu\n", (unsigned long long)id);
  line(row);
}
void genesis_replay_file_loaded(const char *name) {
  if (!active)
    return;
  FILE *f = fopen(name, "rb");
  void *b = NULL;
  long n = 0;
  int ok = 0;
  if (f && !fseek(f, 0, SEEK_END) && (n = ftell(f)) > 0 && n <= MAX_SNAPSHOT &&
      !fseek(f, 0, SEEK_SET)) {
    b = malloc((size_t)n);
    ok = b && fread(b, 1, (size_t)n, f) == (size_t)n;
  }
  if (f)
    fclose(f);
  if (!ok || pending)
    fail("cannot attach loaded state file");
  else if (line("L\n"))
    snapshot(b, (size_t)n);
  free(b);
}
void genesis_replay_save_requested(void) {
  if (active)
    line("W\n");
}
void genesis_replay_sram_flushed(void) {
  if(active)line("P\n");
}

static int playback_command(char *row, size_t cap) {
  unsigned long long id;
  unsigned count;
  char extra;
  for (;;) {
    if (!fgets(row, (int)cap, stream)) {
      fail("incomplete report: no normal end marker (possible hang/crash)");
      return 0;
    }
    if (sscanf(row, "K %llu %c", &id, &extra) == 1) {
      if (!id || id > key_count)
        break;
      ReplayKey *k = &keys[id];
      if (!k->uses)
        continue;
      if (k->blob)
        break;
      size_t n = genesis_rb_bound();
      const GameVideo *video=g_game_spec.video;
      size_t local=video && video->presentation_state_save?
          video->presentation_state_save(NULL,0):0;
      if (video && video->presentation_state_save &&
          (!local || !video->presentation_state_load || local>MAX_SNAPSHOT))break;
      if (n > MAX_CACHE - cache_bytes || local>MAX_CACHE-cache_bytes-n) {
        fail("playback checkpoint memory limit reached");
        return 0;
      }
      /* Match local_states: capture the local scene before its core blob.
       * Object-loading caches can affect the next simulation after rewind,
       * even though disposable display history is excluded from RB digests. */
      if(local) {
        k->presentation=malloc(local);
        if(!k->presentation || video->presentation_state_save(k->presentation,local)!=local)break;
        k->presentation_size=local;
      }
      k->blob = malloc(n);
      k->size = k->blob ? genesis_rb_save_untracked(k->blob, n) : 0;
      if (!k->size)
        break;
      cache_bytes += k->size+k->presentation_size;
      continue;
    }
    if (sscanf(row, "R %llu %c", &id, &extra) == 1) {
      if (!id || id > key_count || !keys[id].uses || !keys[id].blob)
        break;
      ReplayKey *k = &keys[id];
      const GameVideo *video=g_game_spec.video;
      if(k->presentation_size && (!video || !video->presentation_state_load ||
          !video->presentation_state_load(k->presentation,k->presentation_size,0)))break;
      if(!genesis_rb_load(k->blob,k->size))break;
      if(g_game_spec.on_state_loaded)g_game_spec.on_state_loaded();
      if(video && video->presentation_reset)video->presentation_reset();
      if(k->presentation_size &&
          !video->presentation_state_load(k->presentation,k->presentation_size,1)) {
        fail("validated local checkpoint restore failed");return 0;
      }
      if (!--k->uses) {
        cache_bytes -= k->size+k->presentation_size;
        free(k->blob);free(k->presentation);
        k->blob=k->presentation=NULL;
      }
      continue;
    }
    if (!strcmp(row, "L\n")) {
      void *b = NULL;
      size_t n = 0;
      if (!read_snapshot(&b, &n))
        break;
      char name[1200];
      snprintf(name, sizeof name, "%s/replay-loaded-state.bin", folder);
      FILE *f = fopen(name, "wb");
      int ok = f && fwrite(b, 1, n, f) == n;
      free(b);
      if (f && fclose(f))
        ok = 0;
      if (ok)
        ok = runner_load_state_file(name);
      remove(name);
      if (!ok)
        break;
      continue;
    }
    if (!strcmp(row, "W\n")) {
      char name[1200];
      snprintf(name, sizeof name, "%s/replay-saved-state.bin", folder);
      if (!runner_save_state_file(name))
        break;
      continue;
    }
    if (!strcmp(row,"P\n")) {
      runner_replay_sram_flushed();
      continue;
    }
    if (sscanf(row, "E %u %c", &count, &extra) == 1) {
      if (count != executions || fgetc(stream) != EOF || ferror(stream))
        break;
      finished = 1;
      fprintf(stderr, "[feedback-replay] all %u recorded executions matched\n",
              executions);
      return 0;
    }
    if (row[0] == 'I' && row[1] == ' ')
      return 1;
    break;
  }
  fail("invalid or unavailable replay checkpoint");
  return 0;
}
int genesis_replay_begin(GenesisSimInput *in) {
  if (!initialized)
    return 1;
  if (playing) {
    if (failed || finished)
      return 0;
    char row[1024], extra;
    unsigned seq, p[6], t[2], human;
    GenesisReplayView *v = &playback_view;
    if (!playback_command(row, sizeof row))
      return 0;
    int n = sscanf(
        row, "I %u %x %x %x %x %x %x %u %u %x %d %d %d %d %d %d %d %c", &seq,
        &p[0], &p[1], &p[2], &p[3], &p[4], &p[5], &t[0], &t[1], &human,
        &v->drawable_w, &v->drawable_h, &v->custom_on, &v->custom_aspect,
        &v->widescreen, &v->cells, &v->pacing, &extra);
    if (n != 17 || seq != executions || t[0] > 1 || t[1] > 1 || human >= 64 ||
        v->drawable_w < 0 || v->drawable_w > 65535 || v->drawable_h < 0 ||
        v->drawable_h > 65535 || v->custom_on < 0 || v->custom_on > 1 ||
        v->custom_aspect < 0 || v->custom_aspect > 3 || v->widescreen < 0 ||
        v->widescreen > 1 || v->cells < 0 || v->cells > 4096 || v->pacing < 0 ||
        v->pacing > 1) {
      fail("invalid frame inputs or display policy");
      return 0;
    }
    memset(in, 0, sizeof *in);
    for (int i = 0; i < 6; ++i) {
      if (p[i] > 0xfff) {
        fail("invalid controller bits");
        return 0;
      }
      in->pad[i] = (uint16_t)p[i];
    }
    in->pad_type[0] = (uint8_t)t[0];
    in->pad_type[1] = (uint8_t)t[1];
    in->human_mask = human;
    if (view_fn)
      view_fn(v, 1);
    pending = 1;
    return 1;
  }
  if (!active)
    return 1;
  if (GenesisReplayWriterError(writer)) {
    fail(GenesisReplayWriterError(writer));
    return 1;
  }
  GenesisReplayView v = {0};
  if (view_fn)
    view_fn(&v, 0);
  char row[512];
  snprintf(row, sizeof row,
           "I %u %x %x %x %x %x %x %u %u %x %d %d %d %d %d %d %d\n", executions,
           in->pad[0], in->pad[1], in->pad[2], in->pad[3], in->pad[4],
           in->pad[5], in->pad_type[0], in->pad_type[1], in->human_mask,
           v.drawable_w, v.drawable_h, v.custom_on, v.custom_aspect,
           v.widescreen, v.cells, v.pacing);
  pending = line(row);
  return 1;
}
void genesis_replay_end(void) {
  if (!pending || failed)
    return;
  pending = 0;
  size_t need = genesis_rb_bound();
  if (need > MAX_SNAPSHOT) {
    fail("machine checkpoint exceeds recording limit");
    return;
  }
  if (scratch_cap < need) {
    void *b = realloc(scratch, need);
    if (!b) {
      fail("cannot allocate checkpoint buffer");
      return;
    }
    scratch = b;
    scratch_cap = need;
  }
  size_t size = genesis_rb_save_untracked(scratch, scratch_cap);
  const void *parts[4];
  size_t sizes[4];
  if (!size || !genesis_rb_replay_parts(scratch, size, parts, sizes)) {
    fail("cannot inspect completed machine");
    return;
  }
  ++executions;
  if (playing) {
    char row[256], extra;
    unsigned seq, want[4];
    if (!fgets(row, sizeof row, stream) ||
        sscanf(row, "H %u %x %x %x %x %c", &seq, &want[0], &want[1], &want[2],
               &want[3], &extra) != 5 ||
        seq != executions) {
      fail("incomplete result after input (possible hang/crash)");
      return;
    }
    static const char *names[] = {"cpu/scheduler", "work RAM",
                                  "machine/audio/input", "game/mod state"};
    for (int i = 0; i < 4; ++i) {
      uint32_t got = hash(parts[i], sizes[i]);
      if (got != want[i]) {
        fprintf(stderr,
                "[feedback-replay] FIRST DIVERGENCE execution=%u %s "
                "actual=%08x expected=%08x\n",
                executions - 1, names[i], got, want[i]);
        fail("checkpoint mismatch");
        break;
      }
    }
  } else {
    text_bytes +=
        64; /* H is at most 58 bytes, including a ten-digit ordinal. */
    if (text_bytes > MAX_TEXT) {
      fail("input history limit reached");
      return;
    }
    if (!GenesisReplayWriterFrame(writer, executions, parts[0], sizes[0],
                                  parts[1], sizes[1], parts[2], sizes[2],
                                  parts[3], sizes[3]))
      fail(GenesisReplayWriterError(writer));
  }
}
void genesis_replay_shutdown(void) {
  if (writer) {
    if (active && !pending && !failed) {
      char row[64];
      snprintf(row, sizeof row, "E %u\n", executions);
      line(row);
    }
    GenesisReplayWriterEnd(writer);
    const char *why = GenesisReplayWriterDestroy(writer);
    writer = NULL;
    if (why)
      fail(why);
  }
  if (stream)
    fclose(stream);
  if (states)
    fclose(states);
  stream = states = NULL;
  if (keys)
    for (unsigned i = 0; i <= key_count; ++i) {
      free(keys[i].blob);free(keys[i].presentation);
    }
  free(keys);
  keys = NULL;
  free(scratch);
  scratch = NULL;
  scratch_cap = 0;
  active = 0;
}

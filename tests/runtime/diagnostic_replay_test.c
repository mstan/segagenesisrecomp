/* ROM-independent exercise of the real recorder, worker and playback parser.
 * The machine fixture makes each pad, state restore and viewport observable. */
#include "app_config.h"
#include "diagnostic_replay.h"
#include "game_spec.h"
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fixture {
  uint32_t part[4];
} Fixture;
typedef struct Snapshot {
  uint64_t token;
  Fixture machine;
} Snapshot;
static Fixture machine;
static GenesisReplayView view;
static uint32_t local_scene, saved_scene;
static size_t local_save(void *data,size_t cap) {
  if(!data)return sizeof local_scene;
  if(cap!=sizeof local_scene)return 0;
  memcpy(data,&local_scene,cap);return cap;
}
static int local_load(const void *data,size_t size,int apply) {
  if(!data || size!=sizeof local_scene)return 0;
  if(apply)memcpy(&local_scene,data,size);
  return 1;
}
static void local_reset(void) { local_scene=0; }
static const GameVideo local_video={.presentation_state_save=local_save,
    .presentation_state_load=local_load,.presentation_reset=local_reset};
void runner_replay_sram_flushed(void) { machine.part[1]+=31; }
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
static int mod_enabled = 1, mod_value = 7;
static char resource[64] = "source.rom";
static int feature_count(void *ctx) {
  (void)ctx;
  return 1;
}
static int feature_get(void *ctx, int index, RecompLauncherCModFeature *out) {
  (void)ctx;
  if (index)
    return 0;
  memset(out, 0, sizeof *out);
  strcpy(out->id, "character");
  strcpy(out->package_id, "fixture");
  out->enabled = mod_enabled;
  out->option_count = 1;
  return 1;
}
static int enable(void *ctx, const char *package, const char *feature, int on) {
  (void)ctx;
  if (strcmp(package, "fixture") || strcmp(feature, "character"))
    return 0;
  if (on && (mod_value != 7 || strcmp(resource, "source.rom")))
    return 0;
  mod_enabled = on;
  return 1;
}
static int option_get(void *ctx, const char *p, const char *f, int index,
                      RecompLauncherCModOption *out) {
  (void)ctx;
  (void)p;
  (void)f;
  if (index)
    return 0;
  memset(out, 0, sizeof *out);
  strcpy(out->id, "strength");
  snprintf(out->value, sizeof out->value, "%d", mod_value);
  return 1;
}
static int option_set(void *ctx, const char *p, const char *f,
                      const char *option, const char *value) {
  (void)ctx;
  (void)p;
  (void)f;
  if (strcmp(option, "strength"))
    return 0;
  mod_value = atoi(value);
  return 1;
}
static int resource_count(void *ctx, const char *p, const char *f) {
  (void)ctx;
  (void)p;
  (void)f;
  return 1;
}
static int resource_get(void *ctx, const char *p, const char *f, int index,
                        RecompLauncherCModResource *out) {
  (void)ctx;
  (void)p;
  (void)f;
  if (index)
    return 0;
  memset(out, 0, sizeof *out);
  strcpy(out->id, "source");
  strcpy(out->path, resource);
  return 1;
}
static int resource_set(void *ctx, const char *p, const char *f, const char *id,
                        const char *value) {
  (void)ctx;
  (void)p;
  (void)f;
  if (strcmp(id, "source"))
    return 0;
  snprintf(resource, sizeof resource, "%s", value);
  return 1;
}
static const RecompLauncherCModProvider mods = {
    .feature_count = feature_count,
    .feature_get = feature_get,
    .feature_enable = enable,
    .feature_option_get = option_get,
    .feature_set_option = option_set,
    .feature_resource_count = resource_count,
    .feature_resource_get = resource_get,
    .feature_resource_set_path = resource_set};
#endif
AppConfig g_app_config;
static unsigned startup_context;
static size_t feedback_context(void *data,size_t size,int load) {
  if(!data && !size && !load)return sizeof startup_context;
  if(size!=sizeof startup_context)return 0;
  if(load)memcpy(&startup_context,data,size);
  else memcpy(data,&startup_context,size);
  return size;
}
const GameSpec g_game_spec = {.short_name = "fixture",.feedback_context=feedback_context,.video=&local_video};
uint32_t genesis_build_exe_fingerprint(void) { return 0x1234; }
const char *genesis_build_info_string(void) { return "fixture"; }
const char *app_config_aspect_mode(int i) { return i == 2 ? "21:9" : "fit"; }
size_t genesis_rb_bound(void) { return sizeof(Snapshot); }
size_t genesis_rb_save_untracked(void *dst, size_t cap) {
  if (cap < sizeof(Snapshot))
    return 0;
  Snapshot s = {0};
  s.machine = machine;
  memcpy(dst, &s, sizeof s);
  return sizeof s;
}
static void save(Snapshot *s) {
  saved_scene=local_scene;
  genesis_rb_save_untracked(s, sizeof *s);
  s->token = genesis_replay_snapshot_saved();
}
int genesis_rb_load(const void *src, size_t n) {
  if (n != sizeof(Snapshot))
    return 0;
  Snapshot s;
  memcpy(&s, src, sizeof s);
  machine = s.machine;
  local_reset(); /* the real RB loader discards abandoned presentation */
  genesis_replay_snapshot_loaded(s.token);
  return 1;
}
int genesis_rb_replay_parts(const void *src, size_t n, const void *p[4],
                            size_t sizes[4]) {
  if (n != sizeof(Snapshot))
    return 0;
  const Snapshot *s = src;
  for (int i = 0; i < 4; ++i) {
    p[i] = &s->machine.part[i];
    sizes[i] = sizeof(uint32_t);
  }
  return 1;
}
int runner_load_state_file(const char *path) {
  FILE *f = fopen(path, "rb");
  int ok = f && fread(&machine, 1, sizeof machine, f) == sizeof machine;
  if (f)
    fclose(f);
  if (ok)
    genesis_replay_file_loaded(path);
  return ok;
}
int runner_save_state_file(const char *path) {
  (void)path;
  machine.part[2] ^= 0x80000000u;
  genesis_replay_save_requested();
  return 1;
}
static void viewport(GenesisReplayView *v, int apply) {
  if (apply)
    view = *v;
  else
    *v = view;
}
static void tick(const GenesisSimInput *in) {
  machine.part[1]+=local_scene++;
#if RECOMP_LAUNCHER
  if (mod_enabled)
    machine.part[0] += (unsigned)mod_value;
#endif
  for (int p = 0; p < 6; ++p)
    machine.part[p % 4] =
        machine.part[p % 4] * 33 + in->pad[p] + in->human_mask;
  machine.part[2] += in->pad_type[0] + in->pad_type[1];
  int w = 0, h = 0;
  genesis_replay_viewport(&w, &h);
  machine.part[3] += (uint32_t)(view.drawable_w + view.custom_aspect);
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 1;
  int play = !strcmp(argv[1], "play");
  startup_context=play?0:12345;
  g_app_config.replay_diagnostics = 1;
  g_app_config.window_scale = 2;
  unsigned char sram[] = {1, 2, 3, 4};
  int loaded = 0;
  if (play) {
    memset(sram, 0, sizeof sram);
    loaded = 1;
    if (!genesis_replay_boot_sram(argv[3], sram, sizeof sram, &loaded) ||
        loaded || sram[0] != 1 || sram[3] != 4)
      return 2;
  }
  machine.part[0] = sram[0] + (unsigned)loaded;
  if (SDL_Init(0))
    return 1;
#if RECOMP_LAUNCHER
  if (play) {
    mod_value = 1;
    resource[0] = 0;
  }
  const RecompLauncherCModProvider *provider = &mods;
#else
  const struct RecompLauncherCModProvider *provider = NULL;
#endif
  if (!genesis_replay_mods(provider, play ? argv[3] : NULL))
    return 1;
  if (!genesis_replay_init(argv[2], play ? argv[3] : NULL, 0x5678, 4096, sram,
                           sizeof sram, 0, "", viewport)) {
    genesis_replay_shutdown();
    SDL_Quit();
    return 2;
  }
  Snapshot saved = {0};
  if(startup_context!=12345)return 2;
  machine.part[0]+=startup_context;
  for (unsigned i = 0; i < 100; ++i) {
    GenesisSimInput in = {0};
    if (!play) {
      if (i == 30)
        break;
      if (i == 5)
        save(&saved);
      if (i == 8)
        runner_save_state_file("fixture-state.bin");
      if (i == 10) {
        runner_replay_sram_flushed();
        genesis_replay_sram_flushed();
      }
      if (i == 15) {
        if(!genesis_rb_load(&saved,sizeof saved) ||
           !local_load(&saved_scene,sizeof saved_scene,1))return 1;
      }
      if (i == 20) {
        char path[1200];
        snprintf(path, sizeof path, "%s/load.bin", argv[2]);
        Fixture disk = {{123, 456, 789, 10}};
        FILE *f = fopen(path, "wb");
        if (!f)
          return 1;
        fwrite(&disk, 1, sizeof disk, f);
        fclose(f);
        if (!runner_load_state_file(path))
          return 1;
        remove(path);
      }
      view.drawable_w = 640 + (int)i;
      view.drawable_h = 448;
      view.custom_on = 1;
      view.custom_aspect = i < 10 ? 0 : 2;
      for (unsigned p = 0; p < 6; ++p)
        in.pad[p] = (uint16_t)((i + 1) * (p + 1));
      in.pad_type[0] = 1;
      in.human_mask = 63;
    }
    if (!genesis_replay_begin(&in))
      break;
    tick(&in);
    genesis_replay_end();
  }
  genesis_replay_shutdown();
  SDL_Quit();
  return genesis_replay_failed() || (play && !genesis_replay_finished()) ? 2
                                                                         : 0;
}

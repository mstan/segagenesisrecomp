/*
 * app_config.c — settings.ini + rom.cfg load/save (see app_config.h).
 */
#include "app_config.h"
#include "input_map.h"
#include "game_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AppConfig g_app_config;

void app_config_defaults(void)
{
    g_app_config.window_scale     = 2;
    g_app_config.fullscreen       = 0;
    g_app_config.linear_filter    = 0;
    g_app_config.widescreen       = 0;
    g_app_config.widescreen_cells = 8;
    g_app_config.custom_widescreen = 0;
    g_app_config.custom_aspect = 0;
    g_app_config.volume           = 100;
    g_app_config.skip_launcher    = 0;
}

/* ---- tiny ini helpers ---------------------------------------------------- */

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

static GenesisButton button_by_name(const char *name)
{
    for (int b = 0; b < GB_COUNT; b++)
        if (strcmp(name, input_button_name((GenesisButton)b)) == 0)
            return (GenesisButton)b;
    return GB_COUNT;   /* not found */
}

/* Parse "button:N" / "axis:N:+" / "axis:N:-" / "none" into a GamepadBind. */
static void parse_pad_bind(const char *v, GamepadBind *out)
{
    out->kind = GP_BIND_NONE; out->code = 0; out->axis_dir = 0;
    if (!strncmp(v, "button:", 7)) {
        out->kind = GP_BIND_BUTTON;
        out->code = atoi(v + 7);
    } else if (!strncmp(v, "axis:", 5)) {
        out->kind = GP_BIND_AXIS;
        out->code = atoi(v + 5);
        const char *sign = strrchr(v, ':');
        out->axis_dir = (sign && sign[1] == '-') ? -1 : +1;
    }
}

static void format_pad_bind(const GamepadBind *b, char *out, size_t n)
{
    if (b->kind == GP_BIND_BUTTON)
        snprintf(out, n, "button:%d", b->code);
    else if (b->kind == GP_BIND_AXIS)
        snprintf(out, n, "axis:%d:%c", b->code, b->axis_dir < 0 ? '-' : '+');
    else
        snprintf(out, n, "none");
}

/* ---- load ---------------------------------------------------------------- */

int app_config_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    char line[512];
    int section = -1;   /* -1 none, 0 video, 1 audio, 2 launcher, 10 p1, 11 p2 */

    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;

        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) continue;
            *end = '\0';
            char *name = s + 1;
            if      (!strcmp(name, "video"))    section = 0;
            else if (!strcmp(name, "audio"))    section = 1;
            else if (!strcmp(name, "launcher")) section = 2;
            else if (!strcmp(name, "mods.widescreen")) section = 3;
            else if (!strcmp(name, "input.p1")) section = 10;
            else if (!strcmp(name, "input.p2")) section = 11;
            else section = -1;
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (section == 0) {
            if      (!strcmp(key, "window_scale"))     g_app_config.window_scale     = atoi(val);
            else if (!strcmp(key, "fullscreen")) {
                /* Tri-state (launcher vocabulary): 0 off, 1 borderless, 2 exclusive. */
                int fs = atoi(val);
                g_app_config.fullscreen = (fs < 0) ? 0 : (fs > 2) ? 2 : fs;
            }
            else if (!strcmp(key, "linear_filter"))    g_app_config.linear_filter    = atoi(val);
            else if (!strcmp(key, "widescreen"))       g_app_config.widescreen       = atoi(val);
            else if (!strcmp(key, "widescreen_cells")) g_app_config.widescreen_cells = atoi(val);
        } else if (section == 1) {
            if (!strcmp(key, "volume")) g_app_config.volume = atoi(val);
        } else if (section == 2) {
            if (!strcmp(key, "skip_launcher")) g_app_config.skip_launcher = atoi(val);
        } else if (section == 3) {
            if (!strcmp(key, "enabled")) g_app_config.custom_widescreen = atoi(val)==1;
            else if (!strcmp(key, "aspect")) {
                int aspect=app_config_aspect_index(val);
                g_app_config.custom_aspect=aspect<0?0:aspect;
            }
        } else if (section == 10 || section == 11) {
            PlayerInput *pi = &g_input_map.p[section == 10 ? 0 : 1];
            if      (!strcmp(key, "device"))   pi->device       = atoi(val);
            else if (!strcmp(key, "pad_type")) pi->pad_type     = atoi(val);
            else if (!strcmp(key, "deadzone")) pi->deadzone_pct = atoi(val);
            else if (!strncmp(key, "key.", 4)) {
                GenesisButton b = button_by_name(key + 4);
                if (b < GB_COUNT) pi->key[b] = atoi(val);
            } else if (!strncmp(key, "pad.", 4)) {
                GenesisButton b = button_by_name(key + 4);
                if (b < GB_COUNT) parse_pad_bind(val, &pi->pad[b]);
            }
        }
    }
    fclose(f);
    return 1;
}

/* ---- save ---------------------------------------------------------------- */

static void write_player(FILE *f, int player)
{
    const PlayerInput *pi = &g_input_map.p[player];
    fprintf(f, "[input.p%d]\n", player + 1);
    fprintf(f, "device = %d        ; 0 none, 1 keyboard, 2 gamepad, 3 both\n", pi->device);
    fprintf(f, "pad_type = %d      ; 0 = 3-button, 1 = 6-button\n", pi->pad_type);
    fprintf(f, "deadzone = %d\n", pi->deadzone_pct);
    for (int b = 0; b < GB_COUNT; b++)
        fprintf(f, "key.%s = %d\n", input_button_name((GenesisButton)b), pi->key[b]);
    for (int b = 0; b < GB_COUNT; b++) {
        char buf[32];
        format_pad_bind(&pi->pad[b], buf, sizeof(buf));
        fprintf(f, "pad.%s = %s\n", input_button_name((GenesisButton)b), buf);
    }
    fprintf(f, "\n");
}

int app_config_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    fprintf(f, "# segagenesisrecomp launcher settings. Edited by the launcher;\n"
               "# hand-editable. SDL scancodes are numeric (see SDL_scancode.h).\n\n");
    fprintf(f, "[video]\n");
    fprintf(f, "window_scale = %d\n",     g_app_config.window_scale);
    fprintf(f, "fullscreen = %d\n",       g_app_config.fullscreen);
    fprintf(f, "linear_filter = %d\n",    g_app_config.linear_filter);
    fprintf(f, "widescreen = %d\n",       g_app_config.widescreen);
    fprintf(f, "widescreen_cells = %d\n\n", g_app_config.widescreen_cells);
    fprintf(f, "[audio]\n");
    fprintf(f, "volume = %d\n\n",         g_app_config.volume);
    fprintf(f, "[launcher]\n");
    fprintf(f, "skip_launcher = %d\n\n",  g_app_config.skip_launcher);
    fprintf(f, "[mods.widescreen]\nenabled = %d\naspect = %s\n\n",
            g_app_config.custom_widescreen, app_config_aspect_mode(g_app_config.custom_aspect));

    write_player(f, 0);
    write_player(f, 1);

    int ok=!ferror(f);
    if(fclose(f)!=0)ok=0;
    return ok;
}

static const char *const custom_aspects[]={"fit","16:9","21:9","32:9"};
const char *app_config_aspect_mode(int aspect)
{
    return custom_aspects[aspect>=0 && aspect<4?aspect:0];
}
int app_config_aspect_index(const char *mode)
{
    if(!mode)return -1;
    if(!strcmp(mode,"adaptive"))return 0;
    for(int i=0;i<4;++i)if(!strcmp(mode,custom_aspects[i]))return i;
    return -1;
}
void app_config_apply_video(const GameVideo *video)
{
    if(video)video->configure(g_app_config.custom_widescreen?
        app_config_aspect_mode(g_app_config.custom_aspect):"off");
}

#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
/* Same built-in feature-provider contract as Super Metroid/F-Zero. It is
 * offered only when the game supplies a custom renderer; no external archive
 * or game-specific addresses belong in the shared settings bridge. */
static char s_mod_settings[600],s_mod_error[128];
static int video_mod_identity(const char *package,const char *feature)
{
    return package && feature && !strcmp(package,"custom-widescreen") && !strcmp(feature,"widescreen");
}
static int video_mod_count(void *ctx) { (void)ctx;return 1; }
#define MOD_COPY(dst,src) snprintf(dst,sizeof(dst),"%s",src)
static int video_mod_package(void *ctx,int i,RecompLauncherCModPackage *out)
{
    (void)ctx;if(!out || i!=0)return 0;
    memset(out,0,sizeof(*out));
    MOD_COPY(out->id,"custom-widescreen");MOD_COPY(out->name,"Widescreen");MOD_COPY(out->version,"1");
    MOD_COPY(out->author,"GenesisRecomp contributors");
    MOD_COPY(out->description,"Opt-in custom scene renderer with expanded scenery, objects and screen-anchored HUD.");
    MOD_COPY(out->license,"PolyForm Noncommercial 1.0.0");
    out->enabled=g_app_config.custom_widescreen;out->option_count=1;return 1;
}
static int video_mod_feature(void *ctx,int i,RecompLauncherCModFeature *out)
{
    (void)ctx;if(!out || i!=0)return 0;
    memset(out,0,sizeof(*out));
    MOD_COPY(out->id,"widescreen");MOD_COPY(out->package_id,"custom-widescreen");
    MOD_COPY(out->package_name,"Widescreen");MOD_COPY(out->package_version,"1");
    MOD_COPY(out->name,"Widescreen");MOD_COPY(out->group,"Presentation");
    MOD_COPY(out->author,"GenesisRecomp contributors");
    MOD_COPY(out->description,"Use the custom scene renderer. Adaptive follows the entire window without a 32:9 cap. Disabled restores native rendering.");
    out->enabled=g_app_config.custom_widescreen;out->option_count=1;
    MOD_COPY(out->status,out->enabled?"Enabled (experimental)":"Disabled (native renderer)");return 1;
}
static int video_mod_option(void *ctx,const char *package,const char *feature,int i,RecompLauncherCModOption *out)
{
    (void)ctx;if(!out || i!=0 || !video_mod_identity(package,feature))return 0;
    memset(out,0,sizeof(*out));out->type=RECOMP_MOD_OPTION_CHOICE;out->step=1;out->choice_count=4;
    MOD_COPY(out->id,"aspect");MOD_COPY(out->label,"Aspect ratio");
    MOD_COPY(out->description,"Fixed 16:9, 21:9, 32:9, or Adaptive to fit the window. Pixels are not stretched.");
    MOD_COPY(out->value,app_config_aspect_mode(g_app_config.custom_aspect));MOD_COPY(out->default_value,"fit");return 1;
}
static int video_mod_choice(void *ctx,const char *package,const char *feature,const char *option,int i,RecompLauncherCModChoice *out)
{
    (void)ctx;if(!out || !option || strcmp(option,"aspect") || i<0 || i>=4 || !video_mod_identity(package,feature))return 0;
    memset(out,0,sizeof(*out));MOD_COPY(out->value,custom_aspects[i]);
    MOD_COPY(out->label,i?custom_aspects[i]:"Adaptive (fit window)");return 1;
}
static int video_mod_enable(void *ctx,const char *package,const char *feature,int on)
{
    (void)ctx;if(!video_mod_identity(package,feature))return 0;
    g_app_config.custom_widescreen=on!=0;return 1;
}
static int video_mod_set(void *ctx,const char *package,const char *feature,const char *option,const char *value)
{
    (void)ctx;int aspect=app_config_aspect_index(value);
    if(!video_mod_identity(package,feature) || !option || strcmp(option,"aspect") || aspect<0)return 0;
    g_app_config.custom_aspect=aspect;return 1;
}
static int video_mod_commit(void *ctx,const char *image)
{
    (void)ctx;(void)image;s_mod_error[0]=0;
    /* Keep any controller rebindings the launcher already persisted. */
    int on=g_app_config.custom_widescreen,aspect=g_app_config.custom_aspect;
    app_config_load(s_mod_settings);
    g_app_config.custom_widescreen=on;g_app_config.custom_aspect=aspect;
    if(app_config_save(s_mod_settings))return 1;
    MOD_COPY(s_mod_error,"Unable to save widescreen settings.ini");return 0;
}
static const char *video_mod_error(void *ctx) { (void)ctx;return s_mod_error; }
const RecompLauncherCModProvider *app_config_video_mods(const GameVideo *video,const char *path)
{
    static RecompLauncherCModProvider provider;
    if(!video)return NULL;
    snprintf(s_mod_settings,sizeof s_mod_settings,"%s",path?path:"");s_mod_error[0]=0;
    memset(&provider,0,sizeof provider);
    provider.package_count=video_mod_count;provider.package_get=video_mod_package;
    provider.feature_count=video_mod_count;provider.feature_get=video_mod_feature;
    provider.feature_option_get=video_mod_option;provider.feature_choice_get=video_mod_choice;
    provider.feature_enable=video_mod_enable;provider.feature_set_option=video_mod_set;
    provider.commit=video_mod_commit;provider.last_error=video_mod_error;
    provider.archive_extension=".genmod";provider.archive_description="GenesisRecomp mod package";
    return &provider;
}
#undef MOD_COPY
#endif

/* ---- rom.cfg ------------------------------------------------------------- */

int rom_cfg_read(const char *path, char *out, size_t out_len)
{
    if (!out || out_len == 0) return 0;
    out[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fgets(out, (int)out_len, f)) {
        char *s = trim(out);
        if (s != out) memmove(out, s, strlen(s) + 1);
    }
    fclose(f);
    return out[0] ? 1 : 0;
}

void rom_cfg_write(const char *path, const char *rom_path)
{
    if (!rom_path || !*rom_path) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "%s\n", rom_path);
    fclose(f);
}

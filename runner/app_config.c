/*
 * app_config.c â€” settings.ini + rom.cfg load/save (see app_config.h).
 */
#include "app_config.h"
#include "input_map.h"
#include "game_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AppConfig g_app_config;
static const char *const shortcut_ids[]={"original","none","south","east","west","north","back","guide","start","left-stick","right-stick","left-shoulder","right-shoulder","up","down","left","right"};
static const char *const shortcut_labels[]={"Original shortcut","Unbound","South button (A / Cross)","East button (B / Circle)","West button (X / Square)","North button (Y / Triangle)","Back / View / Select","Guide / Home","Start / Menu","Left stick press","Right stick press","Left shoulder (LB / L1)","Right shoulder (RB / R1)","D-pad Up","D-pad Down","D-pad Left","D-pad Right"};
const char *input_shortcut_button_id(int code)
{return code>=-2&&code<15?shortcut_ids[code+2]:"none";}
const char *input_shortcut_button_label(int code)
{return code>=-2&&code<15?shortcut_labels[code+2]:"Unbound";}
int input_shortcut_button_code(const char *id)
{if(id)for(int i=0;i<17;++i)if(!strcmp(id,shortcut_ids[i]))return i-2;return -3;}
static int s_default_custom_widescreen, s_default_custom_aspect;

int app_config_set_video_default(const char *mode)
{
    if (!mode || !strcmp(mode, "off")) {
        s_default_custom_widescreen = 0;
        s_default_custom_aspect = 0;
        return 1;
    }
    int aspect = app_config_aspect_index(mode);
    if (aspect < 0) return 0;
    s_default_custom_widescreen = 1;
    s_default_custom_aspect = aspect;
    return 1;
}

void app_config_defaults(void)
{
    g_app_config.window_scale     = 2;
    g_app_config.fullscreen       = 0;
    g_app_config.linear_filter    = 0;
    g_app_config.internal_scale   = 1;
    g_app_config.widescreen       = 0;
    g_app_config.widescreen_cells = 8;
    g_app_config.custom_widescreen = s_default_custom_widescreen;
    g_app_config.custom_aspect = s_default_custom_aspect;
    g_app_config.presentation_rate = 0;
    g_app_config.audio_filter_bypass = 0;
    g_app_config.volume           = 100;
    g_app_config.audio_freq       = 48000;
    g_app_config.skip_launcher    = 0;
    g_app_config.rewind_enabled   = 1;
    g_app_config.rewind_depth     = 60;
    g_app_config.rewind_interval  = 6;
    g_app_config.replay_diagnostics = 1;
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
            else if (!strcmp(name, "rewind"))   section = 4;
            else if (!strcmp(name, "mods.diagnostics")) section = 5;
            else if (!strcmp(name,"controller.states")) section=6;
            else if (!strcmp(name, "input.p1")) section = 10;
            else if (!strcmp(name, "input.p2")) section = 11;
            else if (!strcmp(name, "input.p3")) section = 12;
            else if (!strcmp(name, "input.p4")) section = 13;
            else if (!strcmp(name, "input.p5")) section = 14;
            else if (!strcmp(name, "input.p6")) section = 15;
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
            else if (!strcmp(key, "internal_scale")) {
                int scale=atoi(val);g_app_config.internal_scale=scale>=1&&scale<=64?scale:1;
            }
            else if (!strcmp(key, "widescreen"))       g_app_config.widescreen       = atoi(val);
            else if (!strcmp(key, "widescreen_cells")) g_app_config.widescreen_cells = atoi(val);
            else if (!strcmp(key,"presentation_rate")) {
                int rate=atoi(val);
                g_app_config.presentation_rate=rate==120 || rate==144 || rate==-1?rate:0;
            }
        } else if (section == 1) {
            if (!strcmp(key, "volume")) g_app_config.volume = atoi(val);
            else if (!strcmp(key,"bypass_filter")) g_app_config.audio_filter_bypass=atoi(val)==1;
            else if (!strcmp(key, "frequency")) {
                int rate = atoi(val);
                g_app_config.audio_freq = rate == 32040 || rate == 32000 ||
                    rate == 44100 || rate == 48000 ? rate : 48000;
            }
        } else if (section == 2) {
            if (!strcmp(key, "skip_launcher")) g_app_config.skip_launcher = atoi(val);
        } else if (section == 3) {
            if (!strcmp(key, "enabled")) g_app_config.custom_widescreen = atoi(val)==1;
            else if (!strcmp(key, "aspect")) {
                int aspect=app_config_aspect_index(val);
                g_app_config.custom_aspect=aspect<0?0:aspect;
            }
        } else if (section == 4) {
            if (!strcmp(key, "enabled")) g_app_config.rewind_enabled = atoi(val) != 0;
            else if (!strcmp(key, "depth")) g_app_config.rewind_depth = atoi(val);
            else if (!strcmp(key, "interval")) g_app_config.rewind_interval = atoi(val);
        } else if (section == 5) {
            if (!strcmp(key,"enabled")) g_app_config.replay_diagnostics=atoi(val)!=0;
        } else if(section==6) {
            InputShortcuts *s=&g_input_map.shortcuts;
            int button=input_shortcut_button_code(val);
            if(!strcmp(key,"enabled"))s->enabled=atoi(val)==1;
            else if(!strcmp(key,"activation"))s->selector=!strcmp(val,"selector");
            else if(button>=-2) {
                if(!strcmp(key,"save"))s->save_button=button;
                else if(!strcmp(key,"load"))s->load_button=button;
                else if(!strcmp(key,"rewind"))s->rewind_button=button;
            }
        } else if (section >= 10 && section < 10 + INPUT_MAX_PLAYERS) {
            PlayerInput *pi = &g_input_map.p[section - 10];
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

void app_config_reload_preserving_mods(const char *path)
{
    int on=g_app_config.custom_widescreen,aspect=g_app_config.custom_aspect,diagnostics=g_app_config.replay_diagnostics;
    int rate=g_app_config.presentation_rate,bypass=g_app_config.audio_filter_bypass;
    InputShortcuts shortcuts=g_input_map.shortcuts;
    app_config_load(path);
    g_app_config.presentation_rate=rate;g_app_config.audio_filter_bypass=bypass;
    g_app_config.custom_widescreen=on;g_app_config.custom_aspect=aspect;
    g_app_config.replay_diagnostics=diagnostics;
    g_input_map.shortcuts=shortcuts;
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
    fprintf(f, "internal_scale = %d\n",   g_app_config.internal_scale);
    fprintf(f, "widescreen = %d\n",       g_app_config.widescreen);
    fprintf(f, "widescreen_cells = %d\n", g_app_config.widescreen_cells);
    fprintf(f, "presentation_rate = %d\n\n", g_app_config.presentation_rate);
    fprintf(f, "[audio]\n");
    fprintf(f, "volume = %d\n",           g_app_config.volume);
    fprintf(f, "frequency = %d\n",      g_app_config.audio_freq);
    fprintf(f, "bypass_filter = %d\n\n", g_app_config.audio_filter_bypass);
    fprintf(f, "[launcher]\n");
    fprintf(f, "skip_launcher = %d\n\n",  g_app_config.skip_launcher);
    fprintf(f, "[rewind]\nenabled = %d\ndepth = %d\ninterval = %d\n\n",
            g_app_config.rewind_enabled, g_app_config.rewind_depth,
            g_app_config.rewind_interval);
    fprintf(f, "[mods.widescreen]\nenabled = %d\naspect = %s\n\n",
            g_app_config.custom_widescreen, app_config_aspect_mode(g_app_config.custom_aspect));
    fprintf(f,"[mods.diagnostics]\nenabled = %d\n\n",g_app_config.replay_diagnostics);
    const InputShortcuts *s=&g_input_map.shortcuts;
    fprintf(f,"[controller.states]\nenabled = %d\nsave = %s\nload = %s\nrewind = %s\nactivation = %s\n\n",
        s->enabled,input_shortcut_button_id(s->save_button),input_shortcut_button_id(s->load_button),
        input_shortcut_button_id(s->rewind_button),s->selector?"selector":"direct");

    for (int p = 0; p < INPUT_MAX_PLAYERS; ++p) write_player(f, p);

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
int app_config_presentation_index(int rate)
{ return rate==120?1:rate==144?2:rate==-1?3:0; }
int app_config_presentation_rate(int index)
{ static const int rates[]={0,120,144,-1};return index>=0 && index<4?rates[index]:0; }
void app_config_apply_video(const GameVideo *video)
{
    if(video && video->presentation_configure)
        video->presentation_configure(g_app_config.presentation_rate!=0);
    if(video && video->configure)video->configure(g_app_config.custom_widescreen?
        app_config_aspect_mode(g_app_config.custom_aspect):"off");
}

#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
/* Same built-in feature-provider contract as Super Metroid/F-Zero. It is
 * offered only when the game supplies a custom renderer; no external archive
 * or game-specific addresses belong in the shared settings bridge. */
static char s_mod_settings[600],s_mod_error[128];
static int s_mod_video_available,s_mod_interp_available;
static int interpolation_identity(const char *p,const char *f)
{ return p && f && !strcmp(p,"high-refresh") && !strcmp(f,"interpolation"); }
static int audio_identity(const char *p,const char *f)
{ return p && f && !strcmp(p,"audio-output") && !strcmp(f,"unfiltered"); }
static const char *const presentation_choices[]={"off","120","144","display"};
static const char *const presentation_labels[]={"Off (original rate)","120 Hz","144 Hz","Display refresh rate"};
static int diagnostic_identity(const char *package,const char *feature) {
    return package && feature && !strcmp(package,"playtest-diagnostics") && !strcmp(feature,"replay");
}
static int video_mod_identity(const char *package,const char *feature)
{
    return package && feature && !strcmp(package,"custom-widescreen") && !strcmp(feature,"widescreen");
}
static int video_mod_count(void *ctx) { (void)ctx;return 3+s_mod_video_available+s_mod_interp_available; }
static int shortcut_identity(const char *p,const char *f)
{return p&&f&&!strcmp(p,"controller-shortcuts")&&!strcmp(f,"state-actions");}
static const char *const shortcut_options[]={"save-button","load-button","rewind-button","activation"};
static const char *const shortcut_option_labels[]={"Save state shortcut","Load state shortcut","Rewind shortcut","Save / load action"};
static int *shortcut_option_value(int i)
{return i==0?&g_input_map.shortcuts.save_button:i==1?&g_input_map.shortcuts.load_button:&g_input_map.shortcuts.rewind_button;}
/* SDL standard shoulder IDs also used by the persisted input map. */
static int shortcut_effective_button(int action,int code)
{return code==INPUT_SHORTCUT_ORIGINAL?(action==0?9:action==1?10:-1):code;}
#define MOD_COPY(dst,src) snprintf(dst,sizeof(dst),"%s",src)
static int video_mod_package(void *ctx,int i,RecompLauncherCModPackage *out)
{
    (void)ctx;if(!out || i<0 || i>=video_mod_count(ctx))return 0;
    memset(out,0,sizeof(*out));
    if(i==2+s_mod_video_available+s_mod_interp_available) {
        MOD_COPY(out->id,"controller-shortcuts");MOD_COPY(out->name,"Controller Shortcuts");MOD_COPY(out->version,"1");
        MOD_COPY(out->description,"Choose, unbind or disable Player 1's controller state shortcuts. Keyboard state controls remain available.");
        out->enabled=g_input_map.shortcuts.enabled;return 1;
    }
    if(i==1+s_mod_video_available+s_mod_interp_available) {
        MOD_COPY(out->id,"audio-output");MOD_COPY(out->name,"Audio");MOD_COPY(out->version,"1");
        MOD_COPY(out->description,"Native sound output options.");out->enabled=g_app_config.audio_filter_bypass;return 1;
    }
    if(s_mod_interp_available && i==1+s_mod_video_available) {
        MOD_COPY(out->id,"high-refresh");MOD_COPY(out->name,"High refresh rate");MOD_COPY(out->version,"1");
        MOD_COPY(out->description,"Interpolate camera and actors between original game ticks. Adds one tick of visual latency; game and audio speed stay unchanged.");
        out->enabled=g_app_config.presentation_rate!=0;out->option_count=1;return 1;
    }
    if(i==s_mod_video_available) {
        MOD_COPY(out->id,"playtest-diagnostics");MOD_COPY(out->name,"Playtest Diagnostics");MOD_COPY(out->version,"1");
        MOD_COPY(out->author,"GenesisRecomp contributors");MOD_COPY(out->license,"PolyForm Noncommercial 1.0.0");
        MOD_COPY(out->description,"Automatically record replayable bug reports in logs. Attach the replay and its sidecars when reporting a problem.");
        out->enabled=g_app_config.replay_diagnostics;return 1;
    }
    MOD_COPY(out->id,"custom-widescreen");MOD_COPY(out->name,"Widescreen");MOD_COPY(out->version,"1");
    MOD_COPY(out->author,"GenesisRecomp contributors");
    MOD_COPY(out->description,"Custom scene renderer with expanded scenery, objects and screen-anchored HUD.");
    MOD_COPY(out->license,"PolyForm Noncommercial 1.0.0");
    out->enabled=g_app_config.custom_widescreen;out->option_count=1;return 1;
}
static int video_mod_feature(void *ctx,int i,RecompLauncherCModFeature *out)
{
    (void)ctx;if(!out || i<0 || i>=video_mod_count(ctx))return 0;
    memset(out,0,sizeof(*out));
    if(i==2+s_mod_video_available+s_mod_interp_available) {
        MOD_COPY(out->id,"state-actions");MOD_COPY(out->package_id,"controller-shortcuts");MOD_COPY(out->package_name,"Controller Shortcuts");
        MOD_COPY(out->name,"Controller State Shortcuts");MOD_COPY(out->group,"Input");
        MOD_COPY(out->description,"Use Player 1's assigned gamepad. Original retains LB save, hold RB load and Back + right stick rewind; three-button gameplay keeps its original shoulder rules. Choose a button or Unbound for each action. Custom shortcut buttons are reserved for that action. Keyboard F7/F8 remain available when disabled.");
        out->enabled=g_input_map.shortcuts.enabled;out->option_count=4;return 1;
    }
    if(i==1+s_mod_video_available+s_mod_interp_available) {
        MOD_COPY(out->id,"unfiltered");MOD_COPY(out->package_id,"audio-output");MOD_COPY(out->package_name,"Audio");
        MOD_COPY(out->name,"Unfiltered audio");MOD_COPY(out->group,"Audio");
        MOD_COPY(out->description,"Bypass native hardware low-pass filters. A separate enhanced FM path retains its own sound.");
        out->enabled=g_app_config.audio_filter_bypass;return 1;
    }
    if(s_mod_interp_available && i==1+s_mod_video_available) {
        MOD_COPY(out->id,"interpolation");MOD_COPY(out->package_id,"high-refresh");MOD_COPY(out->package_name,"High refresh rate");
        MOD_COPY(out->name,"High refresh rate");MOD_COPY(out->group,"Presentation");
        MOD_COPY(out->description,"Smooth camera and actors at 120 Hz, 144 Hz or the display refresh. Original simulation and audio rate; one tick of visual latency. Unsupported scenes repeat their completed image.");
        out->enabled=g_app_config.presentation_rate!=0;out->option_count=1;return 1;
    }
    if(i==s_mod_video_available) {
        MOD_COPY(out->id,"replay");MOD_COPY(out->package_id,"playtest-diagnostics");
        MOD_COPY(out->package_name,"Playtest Diagnostics");MOD_COPY(out->package_version,"1");
        MOD_COPY(out->name,"Replayable feedback");MOD_COPY(out->group,"Diagnostics");
        MOD_COPY(out->author,"GenesisRecomp contributors");
        MOD_COPY(out->description,"Default on. Records controller inputs, settings, mods and state checks for offline reproduction. Zip the game's feedback replay and all sidecars from logs after a problem.");
        MOD_COPY(out->status,g_app_config.replay_diagnostics?"Recording on next launch":"Disabled");
        out->enabled=g_app_config.replay_diagnostics;return 1;
    }
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
    (void)ctx;
    if(shortcut_identity(package,feature)) {
        if(!out||i<0||i>=4)return 0;memset(out,0,sizeof *out);out->type=RECOMP_MOD_OPTION_CHOICE;
        MOD_COPY(out->id,shortcut_options[i]);MOD_COPY(out->label,shortcut_option_labels[i]);
        MOD_COPY(out->description,i==3?"Open the full state selector, or save directly and hold to load.":"Original keeps the existing shortcut. Unbound removes it.");
        MOD_COPY(out->value,i==3?(g_input_map.shortcuts.selector?"selector":"direct"):input_shortcut_button_id(*shortcut_option_value(i)));
        MOD_COPY(out->default_value,i==3?"direct":"original");out->choice_count=i==3?2:17;return 1;
    }
    if(out && i==0 && s_mod_interp_available && interpolation_identity(package,feature)) {
        memset(out,0,sizeof(*out));out->type=RECOMP_MOD_OPTION_CHOICE;out->step=1;out->choice_count=4;
        MOD_COPY(out->id,"rate");MOD_COPY(out->label,"Presentation rate");
        MOD_COPY(out->value,presentation_choices[app_config_presentation_index(g_app_config.presentation_rate)]);
        MOD_COPY(out->default_value,"off");return 1;
    }
    if(!out || i!=0 || !video_mod_identity(package,feature))return 0;
    memset(out,0,sizeof(*out));out->type=RECOMP_MOD_OPTION_CHOICE;out->step=1;out->choice_count=4;
    MOD_COPY(out->id,"aspect");MOD_COPY(out->label,"Aspect ratio");
    MOD_COPY(out->description,"Fixed 16:9, 21:9, 32:9, or Adaptive to fit the window. Pixels are not stretched.");
    MOD_COPY(out->value,app_config_aspect_mode(g_app_config.custom_aspect));MOD_COPY(out->default_value,"fit");return 1;
}
static int video_mod_choice(void *ctx,const char *package,const char *feature,const char *option,int i,RecompLauncherCModChoice *out)
{
    (void)ctx;
    if(shortcut_identity(package,feature)) {
        int n=0;while(n<4&&(!option||strcmp(option,shortcut_options[n])))++n;
        if(!out||n==4||i<0||i>=(n==3?2:17))return 0;memset(out,0,sizeof *out);
        MOD_COPY(out->value,n==3?(i?"selector":"direct"):input_shortcut_button_id(i-2));
        MOD_COPY(out->label,n==3?(i?"Open state menu":"Save / hold to load"):input_shortcut_button_label(i-2));return 1;
    }
    if(out && option && !strcmp(option,"rate") && i>=0 && i<4 && s_mod_interp_available && interpolation_identity(package,feature)) {
        memset(out,0,sizeof(*out));MOD_COPY(out->value,presentation_choices[i]);MOD_COPY(out->label,presentation_labels[i]);return 1;
    }
    if(!out || !option || strcmp(option,"aspect") || i<0 || i>=4 || !video_mod_identity(package,feature))return 0;
    memset(out,0,sizeof(*out));MOD_COPY(out->value,custom_aspects[i]);
    MOD_COPY(out->label,i==3?"32:9 (experimental)":i?custom_aspects[i]:"Adaptive (fit window)");return 1;
}
static int video_mod_enable(void *ctx,const char *package,const char *feature,int on)
{
    if(shortcut_identity(package,feature)){g_input_map.shortcuts.enabled=on!=0;return 1;}
    if(audio_identity(package,feature)) {g_app_config.audio_filter_bypass=on!=0;return 1;}
    if(s_mod_interp_available && interpolation_identity(package,feature)) {
        g_app_config.presentation_rate=on?(g_app_config.presentation_rate?g_app_config.presentation_rate:120):0;return 1;
    }
    if(diagnostic_identity(package,feature)) {g_app_config.replay_diagnostics=on!=0;return 1;}
    (void)ctx;if(!video_mod_identity(package,feature))return 0;
    g_app_config.custom_widescreen=on!=0;return 1;
}
static int video_mod_set(void *ctx,const char *package,const char *feature,const char *option,const char *value)
{
    (void)ctx;
    if(shortcut_identity(package,feature)) {
        s_mod_error[0]=0;int n=0;while(n<4&&(!option||strcmp(option,shortcut_options[n])))++n;
        if(n==4||!value)return 0;
        if(n==3){if(strcmp(value,"direct")&&strcmp(value,"selector"))return 0;g_input_map.shortcuts.selector=!strcmp(value,"selector");return 1;}
        int code=input_shortcut_button_code(value);if(code<INPUT_SHORTCUT_ORIGINAL)return 0;
        int button=shortcut_effective_button(n,code);
        for(int i=0;button>=0&&i<3;++i)if(i!=n&&shortcut_effective_button(i,*shortcut_option_value(i))==button){MOD_COPY(s_mod_error,"That button already has a state shortcut. Unbind it there first.");return 0;}
        *shortcut_option_value(n)=code;return 1;
    }
    if(s_mod_interp_available && interpolation_identity(package,feature) && option && !strcmp(option,"rate") && value) {
        for(int n=0;n<4;++n)if(!strcmp(value,presentation_choices[n])) {g_app_config.presentation_rate=app_config_presentation_rate(n);return 1;}
        return 0;
    }
    int aspect=app_config_aspect_index(value);
    if(!video_mod_identity(package,feature) || !option || strcmp(option,"aspect") || aspect<0)return 0;
    g_app_config.custom_aspect=aspect;return 1;
}
static int video_mod_commit(void *ctx,const char *image)
{
    (void)ctx;(void)image;s_mod_error[0]=0;
    /* Keep any controller rebindings the launcher already persisted. */
    app_config_reload_preserving_mods(s_mod_settings);
    if(app_config_save(s_mod_settings))return 1;
    MOD_COPY(s_mod_error,"Unable to save widescreen settings.ini");return 0;
}
static const char *video_mod_error(void *ctx) { (void)ctx;return s_mod_error; }
const RecompLauncherCModProvider *app_config_video_mods(const GameVideo *video,const char *path)
{
    static RecompLauncherCModProvider provider;
    s_mod_video_available=game_video_has_viewport(video);
    s_mod_interp_available=video && video->interpolate && video->presentation_capture && video->presentation_configure;
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

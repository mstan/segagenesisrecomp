/*
 * app_config.c — settings.ini + rom.cfg load/save (see app_config.h).
 */
#include "app_config.h"
#include "input_map.h"

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
            else if (!strcmp(key, "fullscreen"))       g_app_config.fullscreen       = atoi(val);
            else if (!strcmp(key, "linear_filter"))    g_app_config.linear_filter    = atoi(val);
            else if (!strcmp(key, "widescreen"))       g_app_config.widescreen       = atoi(val);
            else if (!strcmp(key, "widescreen_cells")) g_app_config.widescreen_cells = atoi(val);
        } else if (section == 1) {
            if (!strcmp(key, "volume")) g_app_config.volume = atoi(val);
        } else if (section == 2) {
            if (!strcmp(key, "skip_launcher")) g_app_config.skip_launcher = atoi(val);
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

    write_player(f, 0);
    write_player(f, 1);

    fclose(f);
    return 1;
}

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

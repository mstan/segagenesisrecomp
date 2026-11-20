/*
 * online_diagnostics.c -- see online_diagnostics.h.
 */
#include "online_diagnostics.h"

#include "app_config.h"
#include "console_capture.h"
#include "game_spec.h"
#include "runtime_evidence.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <direct.h>
#  define od_mkdir(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/utsname.h>
#  define od_mkdir(p) mkdir((p), 0755)
#endif

#ifndef GENESIS_GAME_VERSION
#define GENESIS_GAME_VERSION "dev"
#endif

const char *exe_relative(const char *filename);   /* main.c */

static char s_rel[160];
static char s_status[256];

const char *online_diagnostics_relative_path(void)
{
    snprintf(s_rel, sizeof s_rel, "logs/%s-online-diagnostics.txt",
             g_game_spec.short_name ? g_game_spec.short_name : "game");
    return s_rel;
}

int online_diagnostics_recording(void) { return console_capture_file_path()[0] != 0; }

const char *online_diagnostics_status(void)
{
    if (online_diagnostics_recording())
        snprintf(s_status, sizeof s_status, "Recording to %s", online_diagnostics_relative_path());
    else if (g_app_config.online_diagnostics)
        snprintf(s_status, sizeof s_status, "On, but %s could not be written",
                 online_diagnostics_relative_path());
    else
        snprintf(s_status, sizeof s_status, "Off");
    return s_status;
}

static void os_description(char *out, size_t cap)
{
#if defined(_WIN32)
    /* RtlGetVersion reports the real version regardless of manifests. */
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
    OSVERSIONINFOW v;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn fn = ntdll ? (RtlGetVersionFn)(void *)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    if (fn && fn(&v) == 0)
        snprintf(out, cap, "Windows %lu.%lu build %lu", (unsigned long)v.dwMajorVersion,
                 (unsigned long)v.dwMinorVersion, (unsigned long)v.dwBuildNumber);
    else
        snprintf(out, cap, "Windows (version unknown)");
#else
    struct utsname u;
    if (uname(&u) == 0) snprintf(out, cap, "%s %s %s", u.sysname, u.release, u.machine);
    else snprintf(out, cap, "unknown");
#endif
}

static int open_file(void)
{
    char dir[600], path[700], header[2048], os[160], when[64];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    snprintf(dir, sizeof dir, "%s", exe_relative("logs"));
    if (od_mkdir(dir) != 0 && errno != EEXIST) {
        fprintf(stderr, "[online-diagnostics] cannot create the logs folder (errno %d)\n", errno);
        return 0;
    }
    snprintf(path, sizeof path, "%s", exe_relative(online_diagnostics_relative_path()));
    os_description(os, sizeof os);
    if (!tm_now || !strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S %z", tm_now))
        snprintf(when, sizeof when, "unknown");
    snprintf(header, sizeof header,
             "%s -- online diagnostics\n"
             "Send this whole file to the developer. It contains the network\n"
             "addresses (IP:port) of this PC and of the players it connected to,\n"
             "and lobby and player names. It contains no passwords.\n"
             "\n"
             "version:   %s\n"
             "build:     %s exe=%08x\n"
             "os:        %s\n"
             "opened:    %s (%.1f s after the game started)\n"
             "\n",
             g_game_spec.display_name ? g_game_spec.display_name : "Genesis game",
             GENESIS_GAME_VERSION, genesis_build_info_string(),
             (unsigned)genesis_build_exe_fingerprint(), os, when,
             console_capture_uptime());
    if (!console_capture_open_file(path, header)) {
        fprintf(stderr, "[online-diagnostics] cannot write %s\n", online_diagnostics_relative_path());
        return 0;
    }
    /* Relative path only: the absolute one can carry the player's account name. */
    fprintf(stderr, "[online-diagnostics] recording to %s (console capture %s)\n",
            online_diagnostics_relative_path(), console_capture_active() ? "on" : "fallback");
    return 1;
}

void online_diagnostics_apply(void)
{
    int want = g_app_config.online_diagnostics != 0;
    if (want == online_diagnostics_recording()) return;
    if (want) {
        (void)open_file();
    } else {
        fprintf(stderr, "[online-diagnostics] stopped\n");
        console_capture_close_file();
    }
}

void online_diagnostics_init(void)
{
    app_config_online_diagnostics_hooks(online_diagnostics_apply, online_diagnostics_status,
                                        online_diagnostics_relative_path);
    online_diagnostics_apply();
}

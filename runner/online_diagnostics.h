/*
 * online_diagnostics.h -- the player-facing "Online diagnostics" mod.
 *
 * When g_app_config.online_diagnostics is on, the captured console output
 * (console_capture.h) -- launcher, lobby, NAT/ICE, netplay startup and
 * periodic link status -- is written to one file a player can send:
 *
 *     <game folder>/logs/<short_name>-online-diagnostics.txt
 *
 * The previous file is kept as "...txt.previous". Turning the mod on in the
 * launcher starts the file at once, including everything printed since the
 * game was opened; it stays on for later launches until turned off.
 */
#ifndef RUNNER_ONLINE_DIAGNOSTICS_H
#define RUNNER_ONLINE_DIAGNOSTICS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register with app_config (makes the Mods entry available) and apply the
 * loaded setting. Call once after settings.ini has been loaded. */
void online_diagnostics_init(void);
/* Open or close the file to match g_app_config.online_diagnostics. */
void online_diagnostics_apply(void);
/* Path relative to the game folder, e.g. "logs/Sonic2-online-diagnostics.txt". */
const char *online_diagnostics_relative_path(void);
/* Launcher status line for the Mods entry. */
const char *online_diagnostics_status(void);
/* 1 while the file is being written. */
int online_diagnostics_recording(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNNER_ONLINE_DIAGNOSTICS_H */

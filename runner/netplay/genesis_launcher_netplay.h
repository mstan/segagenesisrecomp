#ifndef GENESIS_LAUNCHER_NETPLAY_H
#define GENESIS_LAUNCHER_NETPLAY_H

#include "recomp_launcher.h"
#include "genesis_netplay.h"

#ifdef __cplusplus
extern "C" {
#endif

void genesis_launcher_netplay_init(const char *game_name, const char *game_version,
                                   const char *lan_registry_path);
const RecompLauncherCNetplayCallbacks *genesis_launcher_netplay_callbacks(void);
void genesis_launcher_netplay_apply_host_caps(RecompLauncherCSettings *settings);
void genesis_launcher_netplay_config_from_launch(
    const RecompLauncherCSettings *settings, GenesisNetplayConfig *config);

#ifdef __cplusplus
}
#endif

#endif

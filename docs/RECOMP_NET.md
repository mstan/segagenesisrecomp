# Genesis netplay host integration

`segagenesisrecomp` vendors `recomp-net` at `external/recomp-net` and exposes an
opt-in two-player delay-synchronized runtime. Game targets enable it after
creating the executable:

```cmake
include("${RECOMP_ROOT}/cmake/GenesisRecompNetplay.cmake")
genesisrecomp_enable_netplay(MyGame GAME_VERSION "dev" ICE)
```

The launcher supplies hosted-lobby or direct-LAN parameters. Automation can
bypass the launcher with these variables:

- `GENESIS_NETPLAY=1`
- `GENESIS_NET_SLOT=0|1`
- `GENESIS_NET_BIND=host:port`
- `GENESIS_NET_PEER=host:port` (empty is valid for the listening host)
- `GENESIS_NET_SESSION_ID=number`
- `GENESIS_NET_DELAY=0..16`
- `GENESIS_NET_INPUT_PLAYER=0|1`
- `GENESIS_NET_TRANSPORT=lan|ice`
- `GENESIS_NET_LOBBY_URL=ws://host:port`

The frame contract is strict: stage one local pad, pump until admission,
publish both slot inputs, run exactly one emulated frame, then advance. During
a locked session the published inputs are the only controller source. Save
states are disabled because this first milestone does not synchronize state.

Games whose two-player mode uses a stacked double-height framebuffer can pass
`PEER_VIEW` to `genesisrecomp_enable_netplay`. While netplay is active and a
double-height frame is present, slot 0 displays the top half and slot 1 the
bottom half. This is presentation-only: the complete native framebuffer is
still used for hashes, screenshots, synchronization, and savestates.
## Launcher startup regression

Online CREATE and START occur before the game machine and renderer are
initialized. `GenesisHostLobbyIdentity.capture_local_config` supplies the
current local settings at both points; it must not read an adopted peer's
configuration or depend on the first gameplay tick. The launcher captures the
guest's own preferences separately before adopting the host's configuration.

Set `GENESIS_LOBBY_SELFTEST_PREBOOT=1` alongside the existing
`GENESIS_LOBBY_SELFTEST=host|guest` to run the room callbacks before machine
initialization. This reproduces the online startup ordering that the ordinary
post-initialization selftest and LAN rooms did not cover. Sonic 2 provides
`tools/validate_netplay_launch.py` for release ZIP validation, including
rematches, four seats, simulated network conditions and state digest checks.

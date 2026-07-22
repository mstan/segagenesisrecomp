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

The initial Sonic 2 implementation renders the complete native image on both
peers, including the complete two-player split-screen view. Per-peer viewport
presentation is intentionally reserved for the fast-follow milestone and must
remain presentation-only.

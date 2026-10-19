# Optional expansion ROM and palette contracts

Games may map an immutable, game-owned buffer at `$400000-$7FFFFF` by setting
`g_rom_ext` and `g_rom_ext_size`. Both default to zero. Existing cartridge,
SRAM, RAM, device and open-bus behavior is retained when no bank is present.
Keep the buffer alive and fixed for the session; the supported size is even
and at most 4 MiB. Bus reads and side-effect-free peeks share the same bounded
word helper. A partial final word reads `FF` on the unavailable byte lane.

The optional per-tile palette uses an array of 2,048 VRAM tile flags. Zero
selects native CRAM; 1 through `GVDP_EXT_LINES` select additional host colors
for line-zero tiles. `gvdp_set_ext_tiles(NULL)` disables it. Colors use Genesis
CRAM words, with separate above/below-water sets and a row split. These
presentation settings remain outside serialized VDP hardware state. Games
must serialize any simulation state their added objects/events require before
enabling quickstates, rewind or netplay. Rewind now honors the existing game
state-unavailable callback, as quickstates already do.

`GameSpec.sram_filename` optionally returns a save basename after launcher
configuration. NULL keeps the ROM-derived filename. Directory components and
oversize names are rejected. Use a separate basename when an optional campaign
interprets native SRAM bytes differently. The generic SRAM import/clear panel
is suppressed for games with this callback because their profile can change
while the launcher is open; those games own profile-aware import/export.

The expansion mapping and palette API are adapted from sonsegajp's
[stage-port framework commit](https://github.com/sonsegajp/segagenesisrecomp/commit/014e63ff4c890abe031f13f33d9fa69cb360cd3c).
Bounds checks, save isolation and state guards were added during integration.
Implementation is portable C and does not change CPU feature requirements or
relax existing compatibility checks. Simulation-changing packs require their
own qualification before networking; compilation is not compatibility proof.

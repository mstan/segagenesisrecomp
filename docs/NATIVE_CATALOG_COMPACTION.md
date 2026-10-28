# Guarded native catalog compaction

`tools/native_catalog_compact.py` accepts ordered `GROUP|directory|prefix` catalog
lists. It emits ordinary `M68kStepModule` metadata referencing the original
exported native operation objects. Source files and generated outputs are hashed;
the compressed alias map records every operation decision.

The pass shares only exactly equal function body text under a common
`m68k_step_bind.h` and compilation profile. Operations with strings, preprocessing,
source-location builtins, local static storage or unsupported translation-unit
structure retain their identity or cause generation to fail. It does not change
the original operation archive. Distinct PC/guard/cycle/operation variants keep
their original priority. Generated shards remain strictly PC sorted. The step
runtime still checks every opcode and extension byte against the current mapping.

For the supported Sega CD source pipeline, add `--canonicalize-catalogs` to
`tools/segacd/native_catalog_build.py`. The driver first validates the original
owned-input bundle, reproduces the consumer registry order, generates metadata,
and sets `SEGACD_COMPACT_CATALOG_DIR` in its initial CMake cache. Omitting the flag
retains the original registry. Direct CMake users may supply a qualified bundle
with that cache variable; an empty value selects the original registry.

This is an optional build optimization under qualification. The source audit's
duplicate count is not a runtime performance claim. Qualify built size, startup,
per-game behavior and runtime costs before adopting a generated bundle.

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

The supported Sega CD source pipeline, `tools/segacd/native_catalog_build.py`,
enables compaction by default. The driver first validates the original
owned-input bundle, reproduces the consumer registry order, generates metadata,
and sets `SEGACD_COMPACT_CATALOG_DIR` in its initial CMake cache. Use
`--no-canonicalize-catalogs` to retain the original registry. Unsupported source
formats report a diagnostic and fall back to that registry. Direct CMake users may supply a qualified bundle
with that cache variable; an empty value selects the original registry.

The qualified CD fixture reduces the Windows executable from142,430,208 to
109,482,496 bytes (23.13%) and the unstripped Linux executable from544,293,880 to
489,968,336 bytes (9.98%). Windows and four Linux3000-frame runs preserve complete
guest state, pixels and WAV. Linux host instruction retirement falls3.73%, but
CPU timing is mixed; startup observations are also neutral/inconclusive. These
are footprint and repeated-work reductions, not a proven frame-rate increase.

Raw matched builds, samples, source hashes, operation aliases and frame evidence
are referenced by `engine-optimization-results.json`. Cartridge consumers do not
use this catalog format and receive no catalog-compaction gain claim.

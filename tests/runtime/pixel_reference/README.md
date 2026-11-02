Frozen reference: engine `426253e1b76056a45c48347ddcd75eb64e31c1d8`.
The source/header bytes and hashes in manifest.json remain independent of the
candidate implementation. Prefixing only exported machine symbols lets the
original and candidate schedulers consume the same VDP/Z80 implementation and
synthetic input fixture. No production source includes this directory.

The machine fixture replaces guest glue and bus I/O, so it qualifies the new
adapter/context, raster/palette/status and lifecycle contracts. Real compiled
game execution and actual SDL/UI/filter/upload presentation are separate gates.

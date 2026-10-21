# Shared owned-disc media tools

`disc_image.py` normalizes supported raw CUE/BIN media to SGCDIMG1.
`sgcd_owned_extract.py` verifies that container, provides bounded ISO9660
reads and extracts original program files for native recompilation builds.
These two tools come from the completed `feature/segacd-backend` implementation.

`owned_media.py` exposes the same program-area reads for either CUE/BIN or
SGCD, without making an intermediate disc copy. It delegates CUE timeline
interpretation to `disc_image.read_cue` and container integrity to `SGCD`.
Track-relative reads exclude pregaps and retain explicitly declared postgaps.
`ISO9660.read_sectors` batches reads where the media supplies a range reader.

Consumers retain their own game identity checks. An SGCD payload hash and a
hash of ISO user-data sectors identify different representations; they must
not be compared as if interchangeable. S3K's CD campaign keeps its qualified
ISO identity and per-track audio hashes, then converts content to S3K formats.
The native CD consumer continues to qualify its original BIOS and raw image.

Run `python tools/segacd/test_sgcd_owned_extract.py` and
`python tools/segacd/test_owned_media.py`. The fixtures contain no owned data.
Consumer tests must also qualify their game-specific transforms and resources.

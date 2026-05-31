#!/usr/bin/env python3
"""
package_release.py — build a compliance-checked release zip.

WHY: every binary we ship statically links clownmdemu + clownz80 (AGPL-3.0),
so a shipped binary is an AGPL combined work. It MUST carry the AGPL license
and MUST NOT contain the ROM (copyright), RAM dumps, saves, or build junk.

This packager builds the zip from an explicit ALLOWLIST (so a ROM can never be
swept in), verifies the AGPL license is present, warns if a ROM is sitting next
to the exe, and refuses to produce a zip if anything forbidden slips through.

See RELEASING.md. Exit code is non-zero on any compliance failure.
"""
import argparse
import fnmatch
import os
import sys
import zipfile

# Forbidden in a release zip. NOTE: ".md" is intentionally NOT here — README.md
# is markdown. Genesis ROMs in this project are *.bin, covered below.
FORBIDDEN = [
    "*.bin", "*.gen", "*.smd", "*.md5", "*.sha256",          # ROM images / hashes
    "ramdump*", "*_save_*.bin", "savestate*", "*.srm",       # dumps / saves
    "*.log", "*.map", "*.obj", "*.pdb", "*.ilk", "*.exp", "*.lib",  # build junk
]


def is_forbidden(name):
    base = os.path.basename(name).lower()
    return any(fnmatch.fnmatch(base, pat) for pat in FORBIDDEN)


def fail(msg):
    sys.stderr.write("ERROR: " + msg + "\n")
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description="Build a compliance-checked release zip.")
    ap.add_argument("--exe", required=True, help="the game .exe")
    ap.add_argument("--out", required=True, help="output .zip path")
    ap.add_argument("--license", required=True, help="AGPL-3.0 LICENSE file")
    ap.add_argument("--notices", required=True, help="THIRD-PARTY-LICENSES.md")
    ap.add_argument("--readme", required=True, help="README for the zip")
    ap.add_argument("--extra", nargs="*", default=[],
                    help="additional allowlisted files (e.g. SDL2.dll)")
    args = ap.parse_args()

    allow = [args.exe, args.license, args.notices, args.readme] + list(args.extra)

    # 1) every allowlisted file must exist
    missing = [p for p in allow if not os.path.isfile(p)]
    if missing:
        fail("missing required files:\n  " + "\n  ".join(missing))

    # 2) no allowlisted file may itself be a forbidden type (defensive)
    bad = [p for p in allow if is_forbidden(p)]
    if bad:
        fail("forbidden file in the allowlist (ROM/dump/junk?):\n  " + "\n  ".join(bad))

    # 3) the LICENSE must actually be AGPL
    with open(args.license, "r", encoding="utf-8", errors="ignore") as fh:
        if "AFFERO GENERAL PUBLIC LICENSE" not in fh.read().upper():
            fail("--license is not AGPL. Binary releases must be AGPL-3.0 (see RELEASING.md).")

    # 4) warn loudly if a ROM/junk file is sitting next to the exe (not packaged,
    #    but a sign someone might zip the folder by hand)
    build_dir = os.path.dirname(os.path.abspath(args.exe))
    strays = sorted(n for n in os.listdir(build_dir) if is_forbidden(n))
    if strays:
        sys.stderr.write(
            "WARNING: forbidden files exist next to the exe (NOT packaged, but "
            "never zip this folder directly):\n  " + "\n  ".join(strays) + "\n")

    # 5) build the zip from the allowlist ONLY
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        for p in allow:
            z.write(p, os.path.basename(p))

    # 6) final audit of the produced zip; delete + fail if anything leaked
    with zipfile.ZipFile(args.out) as z:
        names = z.namelist()
    leaked = [n for n in names if is_forbidden(n)]
    if leaked:
        os.remove(args.out)
        fail("forbidden files leaked into the zip; aborted:\n  " + "\n  ".join(leaked))

    print("OK: wrote " + args.out)
    for n in names:
        print("   + " + n)


if __name__ == "__main__":
    main()

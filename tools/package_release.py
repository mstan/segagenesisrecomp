#!/usr/bin/env python3
"""
package_release.py — build a compliance-checked release zip.

WHY: release (native) binaries are AGPL-FREE (own backend; see RELEASING.md)
and ship under PolyForm Noncommercial 1.0.0 + permissive third-party notices.
The zip MUST NOT contain the ROM (copyright), RAM dumps, saves, build junk,
an `_oracle` exe, or `GenesisRecomp.exe` (those statically link AGPL code and
are dev-only).

This packager builds the zip from an explicit ALLOWLIST (so a ROM can never be
swept in), verifies the license is PolyForm Noncommercial (and NOT AGPL), warns
if a ROM is sitting next to the exe, and refuses to produce a zip if anything
forbidden slips through.

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
    ap.add_argument("--license", required=True,
                    help="project LICENSE file (PolyForm Noncommercial 1.0.0)")
    ap.add_argument("--notices", required=True, help="THIRD-PARTY-LICENSES.md")
    ap.add_argument("--readme", required=True, help="README for the zip")
    ap.add_argument("--extra", nargs="*", default=[],
                    help="additional allowlisted files (e.g. SDL2.dll)")
    ap.add_argument("--asset-dir", nargs="*", default=[],
                    help="directories staged recursively into the zip preserving "
                         "their top folder name (e.g. build/Release/launcher -> "
                         "launcher/...). Used for the pre-boot launcher UI assets.")
    args = ap.parse_args()

    allow = [args.exe, args.license, args.notices, args.readme] + list(args.extra)

    # 1) every allowlisted file must exist
    missing = [p for p in allow if not os.path.isfile(p)]
    if missing:
        fail("missing required files:\n  " + "\n  ".join(missing))

    # 1b) gather asset-dir files as (abspath, arcname), preserving each dir's own
    #     top folder name -> launcher/launcher.rml, launcher/fonts/*, launcher/img/*
    asset_files = []  # (src_path, arcname)
    for d in args.asset_dir:
        if not os.path.isdir(d):
            fail("missing --asset-dir: " + d)
        d = os.path.abspath(d.rstrip("/\\"))
        top = os.path.basename(d)
        for root, _dirs, files in os.walk(d):
            for fn in files:
                src = os.path.join(root, fn)
                rel = os.path.relpath(src, d).replace(os.sep, "/")
                asset_files.append((src, top + "/" + rel))
    # asset files face the same forbidden-type screen as everything else
    bad_assets = [a for (s, a) in asset_files if is_forbidden(a)]
    if bad_assets:
        fail("forbidden file under --asset-dir (ROM/dump/junk?):\n  "
             + "\n  ".join(bad_assets))

    # 2) no allowlisted file may itself be a forbidden type (defensive)
    bad = [p for p in allow if is_forbidden(p)]
    if bad:
        fail("forbidden file in the allowlist (ROM/dump/junk?):\n  " + "\n  ".join(bad))

    # 3) the exe must be a NATIVE release target — never the oracle or the
    #    recompiler tool (both statically link AGPL code; dev-only)
    exe_base = os.path.basename(args.exe).lower()
    if "_oracle" in exe_base or exe_base == "genesisrecomp.exe":
        fail("refusing to package a dev-only AGPL-linked exe (%s); release the "
             "native target (see RELEASING.md)." % exe_base)

    # 4) the LICENSE must be PolyForm Noncommercial — and must NOT be AGPL
    #    (release binaries are AGPL-free; see RELEASING.md)
    with open(args.license, "r", encoding="utf-8", errors="ignore") as fh:
        lic = fh.read().upper()
    if "AFFERO GENERAL PUBLIC LICENSE" in lic:
        fail("--license is AGPL, but release binaries are AGPL-free and ship "
             "under PolyForm Noncommercial 1.0.0 (see RELEASING.md).")
    if "POLYFORM NONCOMMERCIAL" not in lic:
        fail("--license is not PolyForm Noncommercial 1.0.0 (see RELEASING.md).")

    # 5) warn loudly if a ROM/junk file is sitting next to the exe (not packaged,
    #    but a sign someone might zip the folder by hand)
    build_dir = os.path.dirname(os.path.abspath(args.exe))
    strays = sorted(n for n in os.listdir(build_dir) if is_forbidden(n))
    if strays:
        sys.stderr.write(
            "WARNING: forbidden files exist next to the exe (NOT packaged, but "
            "never zip this folder directly):\n  " + "\n  ".join(strays) + "\n")

    # 6) build the zip from the allowlist ONLY. The license lands as "LICENSE"
    #    regardless of its on-disk name (RELEASING.md checklist item 2).
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        for p in allow:
            arc = "LICENSE" if p == args.license else os.path.basename(p)
            z.write(p, arc)
        for src, arc in asset_files:
            z.write(src, arc)

    # 7) final audit of the produced zip; delete + fail if anything leaked
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

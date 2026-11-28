"""Stage one verified NRO and its runtime assets in an SD-card layout."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("build", "target", "short", "rom", "out"):
        parser.add_argument("--" + flag, required=True)
    parser.add_argument("--with-roms", action="store_true")
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--engine", type=Path)
    args = parser.parse_args()
    build, rom = Path(args.build), Path(args.rom)
    nro = build / (args.target + ".nro")
    data = nro.read_bytes()
    if data[16:20] != b"NRO0":
        raise ValueError("Invalid NRO header")
    image_size = struct.unpack_from("<I", data, 24)[0]
    if data[image_size:image_size + 4] != b"ASET":
        raise ValueError("Missing NRO assets/metadata")
    assets = build / "assets"
    if not (assets / "fonts").is_dir() or not (assets / "img").is_dir():
        raise ValueError("Launcher runtime assets were not staged")
    dest = Path(args.out) / "switch" / args.target
    if not args.with_roms and dest.exists():
        # Public packaging must never inherit a private image or old state from
        # a previously populated SD folder. Use a fresh staging destination.
        raise ValueError("Public staging destination already exists; choose a fresh --out")
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(nro, dest / nro.name)
    shutil.copytree(assets, dest / "assets", dirs_exist_ok=True)
    licenses = dest / "licenses"
    licenses.mkdir(exist_ok=True)
    for root, label, names in (
        (args.engine, "engine", ("LICENSE.md", "THIRD-PARTY-LICENSES.md")),
        (args.repo, "game", ("LICENSE.md",)),
        (args.repo / "recomp-ui" if args.repo else None, "recomp-ui", ("LICENSE",)),
    ):
        if root:
            for name in names:
                if (root / name).is_file():
                    shutil.copy2(root / name, licenses / (label + "-" + name))
    if (build / "docs").is_dir():
        shutil.copytree(build / "docs", dest / "docs", dirs_exist_ok=True)
    # Writable SD data remains outside romfs. Do not reset existing choices.
    settings = dest / "settings.ini"
    if not settings.exists():
        settings.write_text(
            "[video]\nfullscreen=1\nlinear_filter=0\n"
            "[audio]\nvolume=100\n[launcher]\nskip_launcher=0\n"
            "[mods.widescreen]\nenabled=1\naspect=adaptive\n"
            "[mods.diagnostics]\nenabled=1\n"
            "[input.p1]\ndevice=2\n",
            encoding="utf-8")
    if args.with_roms:
        copied_rom = dest / (args.short.lower() + ".bin")
        shutil.copy2(rom, copied_rom)
        if digest(rom) != digest(copied_rom):
            raise ValueError("ROM copy differs from original")
        cfg = dest / ("rom-" + args.short + ".cfg")
        if not cfg.exists():
            cfg.write_text(copied_rom.name + "\n", encoding="utf-8")
    manifest = {"target": args.target, "nro_sha256": digest(dest / nro.name),
                "rom_included": args.with_roms, "input_rom_sha256": digest(rom),
                "validation": "cross-compiled; Switch hardware playtest pending"}
    for label, root in (("game", args.repo), ("engine", args.engine)):
        if root:
            manifest[label + "_commit"] = subprocess.check_output(
                ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    manifest["files"] = {
        str(p.relative_to(dest)).replace("\\", "/"): digest(p)
        for p in sorted(dest.rglob("*"))
        if p.is_file() and p.name != "build-info-switch.json"
    }
    (dest / "build-info-switch.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Staged {dest}")


if __name__ == "__main__":
    main()

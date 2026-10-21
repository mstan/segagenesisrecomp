#!/usr/bin/env python3
"""Create and validate a portable Windows package for the Sonic CD native runner.

The package contains the executable, SDL2 runtime, license texts, and launcher
scripts. BIOS/disc paths are stored as text only; owner media is never copied.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import shutil
import sys


SCHEMA = "SegaCDWindowsPortablePackage1"
BACKUP_BYTES = 8192
SOURCE_ROOT = Path(__file__).resolve().parents[2]
# The SDL runtime UI has no external font/image assets. Ship the notices for
# the code actually linked into this standalone target, plus the project terms.
SOURCE_NOTICES = {
    "LICENSE": "LICENSE.md",
    "THIRD-PARTY-LICENSES.md": "THIRD-PARTY-LICENSES.md",
    "licenses/SDL2.txt": "runner/external/SDL2/SDL2-2.28.5/COPYING.txt",
    "licenses/ymfm.txt": "runner/external/ymfm/LICENSE",
    "licenses/ares.txt": "runner/cd/ARES-LICENSE.txt",
    "licenses/m68k-recomp-core.txt": "external/m68k-recomp-core/LICENSE",
    "licenses/z80-recomp-core.txt": "external/z80-recomp-core/LICENSE",
}
PACKAGE_FILES = (
    "SonicCD.exe",
    "SDL2.dll",
    "SonicCD.ps1",
    "SonicCD.cmd",
    "README-portable-windows.md",
    "tools/segacd/disc_image.py",
    "LICENSE",
    "THIRD-PARTY-LICENSES.md",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_c_drive(path: str | os.PathLike[str]) -> bool:
    """Return true for a Windows C: path without resolving it on this host."""
    value = os.fspath(path)
    drive = PureWindowsPath(value).drive
    return drive.casefold() == "c:"


def require_file(path: Path, label: str) -> Path:
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise ValueError(f"{label} does not exist or is unreadable: {path}") from error
    if not resolved.is_file():
        raise ValueError(f"{label} is not a regular file: {path}")
    return resolved


def _media_path(value: str | None, label: str, allowed_suffixes: set[str]) -> str | None:
    if value is None:
        return None
    path = require_file(Path(value), label)
    if path.suffix.casefold() not in allowed_suffixes:
        suffixes = ", ".join(sorted(allowed_suffixes))
        raise ValueError(f"{label} must use one of these extensions: {suffixes}")
    return str(path)


def _copy_licenses(source: Path, destination: Path) -> list[dict[str, object]]:
    if not source.is_dir():
        raise ValueError(f"license directory does not exist: {source}")
    files = sorted(source.iterdir(), key=lambda item: item.name.casefold())
    if not files:
        raise ValueError(f"license directory is empty: {source}")
    copied: list[dict[str, object]] = []
    destination.mkdir(parents=True, exist_ok=True)
    for item in files:
        if item.is_symlink() or not item.is_file():
            raise ValueError(f"license directory may contain only regular files: {item}")
        target = destination / item.name
        shutil.copyfile(item, target)
        copied.append({"path": f"licenses/{item.name}", "bytes": target.stat().st_size,
                       "sha256": sha256(target)})
    return copied


def create_package(args: argparse.Namespace) -> dict[str, object]:
    executable = require_file(args.exe, "runner executable")
    sdl = require_file(args.sdl2_dll, "SDL2 runtime")
    if executable.suffix.casefold() != ".exe":
        raise ValueError("runner executable must be an .exe file")
    if sdl.name.casefold() != "sdl2.dll":
        raise ValueError("SDL2 runtime must be named SDL2.dll")
    destination = args.destination.resolve(strict=False)
    if is_c_drive(destination):
        raise ValueError("package output on C: is refused; choose an F: or G: destination")
    if destination.exists():
        raise ValueError(f"package destination already exists: {destination}")

    bios = _media_path(args.bios, "BIOS", {".bin", ".rom"})
    disc = _media_path(args.disc, "disc", {".sgcd", ".cue"})
    seed: Path | None = None
    if args.blank_backup_seed is not None:
        seed = require_file(args.blank_backup_seed, "blank backup seed")
        if seed.stat().st_size != BACKUP_BYTES:
            raise ValueError(f"blank backup seed must be exactly {BACKUP_BYTES} bytes")

    license_dir = args.licenses_dir or executable.parent / "licenses"
    license_dir = license_dir.resolve(strict=True)
    if not license_dir.is_dir():
        raise ValueError(f"license directory is not a directory: {license_dir}")
    notices = {name: require_file(SOURCE_ROOT / relative, "source notice")
               for name, relative in SOURCE_NOTICES.items()}

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    try:
        shutil.copyfile(executable, destination / "SonicCD.exe")
        shutil.copyfile(sdl, destination / "SDL2.dll")
        shutil.copyfile(Path(__file__).with_name("SonicCD.ps1"), destination / "SonicCD.ps1")
        shutil.copyfile(Path(__file__).with_name("SonicCD.cmd"), destination / "SonicCD.cmd")
        shutil.copyfile(Path(__file__).with_name("README-portable-windows.md"),
                        destination / "README-portable-windows.md")
        tools_dir = destination / "tools" / "segacd"
        tools_dir.mkdir(parents=True)
        shutil.copyfile(Path(__file__).with_name("disc_image.py"), tools_dir / "disc_image.py")
        license_files = _copy_licenses(license_dir, destination / "licenses")
        notice_names = set(SOURCE_NOTICES)
        license_files = [row for row in license_files if row["path"] not in notice_names]
        for relative, source in notices.items():
            target = destination / relative
            shutil.copyfile(source, target)
            if relative.startswith("licenses/"):
                license_files.append({"path": relative, "bytes": target.stat().st_size,
                                      "sha256": sha256(target)})
        data_dir = destination / "userdata"
        data_dir.mkdir()
        seed_dir = destination / "seed"
        seed_dir.mkdir()
        if seed is not None:
            shutil.copyfile(seed, seed_dir / "sonic-cd.brm")
        config = {
            "schema": SCHEMA,
            "bios_path": bios,
            "disc_path": disc,
        }
        config_path = data_dir / "portable-config.json"
        config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

        records = []
        for relative in PACKAGE_FILES:
            path = destination / relative
            records.append({"path": relative, "bytes": path.stat().st_size,
                            "sha256": sha256(path)})
        records.extend(license_files)
        if seed is not None:
            path = destination / "seed" / "sonic-cd.brm"
            records.append({"path": "seed/sonic-cd.brm", "bytes": path.stat().st_size,
                            "sha256": sha256(path)})
        manifest = {
            "schema": SCHEMA,
            "runner_sha256": sha256(destination / "SonicCD.exe"),
            "sdl2_sha256": sha256(destination / "SDL2.dll"),
            "licenses": license_files,
            "owner_media_copied": False,
            "blank_backup_seed_included": seed is not None,
            "files": records,
        }
        (destination / "package-manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return {"status": "created", "package": str(destination),
                "manifest": str(destination / "package-manifest.json"),
                "runner_sha256": manifest["runner_sha256"],
                "sdl2_sha256": manifest["sdl2_sha256"],
                "owner_media_copied": False}
    except Exception:
        # Destination was required not to exist before this call. Leave a
        # partial package visible for diagnosis rather than deleting user data.
        raise


def validate_package(args: argparse.Namespace) -> dict[str, object]:
    package = args.package.resolve(strict=True)
    if not package.is_dir():
        raise ValueError(f"package is not a directory: {package}")
    manifest_path = package / "package-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (manifest.get("schema") != SCHEMA or
            manifest.get("owner_media_copied") is not False or
            not isinstance(manifest.get("files"), list)):
        raise ValueError("package manifest schema or media-copy policy is invalid")
    checked = []
    seen: set[str] = set()
    for record in manifest.get("files", []):
        if not isinstance(record, dict) or not isinstance(record.get("path"), str):
            raise ValueError("package manifest contains an invalid file record")
        relative_text = record["path"].replace("\\", "/")
        windows_relative = PureWindowsPath(relative_text)
        relative = Path(*relative_text.split("/"))
        if (windows_relative.is_absolute() or windows_relative.drive or
                any(part in ("", ".", "..") for part in relative.parts) or
                not isinstance(record.get("bytes"), int) or record["bytes"] < 0 or
                not isinstance(record.get("sha256"), str) or len(record["sha256"]) != 64):
            raise ValueError(f"unsafe or malformed package file record: {relative_text}")
        normalized = relative.as_posix().casefold()
        if normalized in seen:
            raise ValueError(f"duplicate package path in manifest: {relative_text}")
        seen.add(normalized)
        try:
            path = (package / relative).resolve(strict=True)
            path.relative_to(package)
        except (OSError, ValueError) as error:
            raise ValueError(f"package path escapes or is missing: {relative_text}") from error
        if not path.is_file():
            raise ValueError(f"package file is missing: {relative}")
        if path.stat().st_size != record["bytes"] or sha256(path) != record["sha256"]:
            raise ValueError(f"package file hash/size mismatch: {relative}")
        checked.append(str(relative).replace("\\", "/"))
    for relative in PACKAGE_FILES:
        if relative not in checked:
            raise ValueError(f"required package file not recorded: {relative}")
    if manifest.get("runner_sha256") != sha256(package / "SonicCD.exe"):
        raise ValueError("runner hash does not match the package manifest")
    if manifest.get("sdl2_sha256") != sha256(package / "SDL2.dll"):
        raise ValueError("SDL2 hash does not match the package manifest")
    config_path = package / "userdata" / "portable-config.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("schema") != SCHEMA:
        raise ValueError("portable configuration schema is invalid")
    return {"status": "valid", "package": str(package),
            "files_verified": len(checked),
            "owner_media_copied": False,
            "bios_configured": bool(config.get("bios_path")),
            "disc_configured": bool(config.get("disc_path"))}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="create a new portable package directory")
    create.add_argument("--exe", required=True, type=Path)
    create.add_argument("--sdl2-dll", required=True, type=Path)
    create.add_argument("--destination", required=True, type=Path)
    create.add_argument("--licenses-dir", type=Path,
                        help="defaults to <runner directory>/licenses")
    create.add_argument("--bios", help="existing BIOS path to store in package config (never copied)")
    create.add_argument("--disc", help="existing SGCD or CUE path to store in config (never copied)")
    create.add_argument("--blank-backup-seed", type=Path,
                        help="optional 8192-byte seed, copied only into the new package")
    validate = commands.add_parser("validate", help="verify a package without launching the game")
    validate.add_argument("--package", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = create_package(args) if args.command == "create" else validate_package(args)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Sega CD package: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Validate an owned-input native catalog bundle and build SonicCD from source.

Catalog generation and BIOS/disc probing are separate steps. This tool consumes
an already-generated, hash-manifested catalog bundle; it never imports archives
listed as historical provenance in the manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zlib

BIOS_BYTES = 128 * 1024
BIOS_SHA256 = "807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a"
BIOS_CRC32 = 0xC6D10268
DISC_PAYLOAD_SHA256 = "4e822010e42092681b238ddc7cbff41802c0cfc006c9b1510dcebd6b9dd452b1"
PREFIX_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ROLES = {"MAIN", "SUB", "Z80"}


class BuildInputError(ValueError):
    pass


def _path_key(path: Path) -> str:
    return os.path.normcase(str(path.resolve(strict=False)))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BuildInputError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise BuildInputError(f"expected a JSON object in {path}")
    return value


def _manifest_files(manifest: dict) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    for collection in ("base_catalogs", "overlay_catalogs"):
        rows = manifest.get(collection)
        if not isinstance(rows, list):
            raise BuildInputError(f"manifest field {collection} must be an array")
        for row in rows:
            if not isinstance(row, dict):
                raise BuildInputError(f"invalid entry in manifest {collection}")
            raw_path, digest, role = row.get("Path"), row.get("Sha256"), row.get("Role")
            if not isinstance(raw_path, str) or not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
                raise BuildInputError(f"invalid path/hash in manifest {collection}")
            path = Path(raw_path).resolve(strict=False)
            key = _path_key(path)
            if key in result:
                raise BuildInputError(f"duplicate manifest path: {path}")
            result[key] = (digest.lower(), str(role))
    return result


def _parse_catalog_list(path: Path) -> list[tuple[str, Path, str]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise BuildInputError(f"cannot read catalog list {path}: {error}") from error
    rows = []
    for line_number, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("|")
        if len(fields) != 3:
            raise BuildInputError(f"{path}:{line_number}: expected ROLE|directory|prefix")
        role, directory, prefix = fields
        role = role.upper()
        if role not in ROLES or not PREFIX_RE.fullmatch(prefix) or not directory:
            raise BuildInputError(f"{path}:{line_number}: invalid role, directory, or prefix")
        if ";" in directory or '"' in directory:
            raise BuildInputError(f"{path}:{line_number}: unsupported CMake path characters")
        rows.append((role, Path(directory).resolve(strict=False), prefix))
    if not rows:
        raise BuildInputError(f"catalog list is empty: {path}")
    return rows


def _expected_catalog_files(role: str, directory: Path, prefix: str) -> set[Path]:
    if role == "Z80":
        return {directory / "sonic_cd_z80_step.c", directory / "sonic_cd_z80_step.h"}
    return {directory / f"{prefix}_module.c", directory / f"{prefix}.h"} | {
        directory / f"{prefix}_part{index:02d}.c" for index in range(32)
    }


def validate_catalog_bundle(manifest_path: Path, base_list: Path, overlay_list: Path) -> dict:
    manifest_path = manifest_path.resolve(strict=True)
    manifest = load_json(manifest_path)
    file_hashes = _manifest_files(manifest)
    base_catalogs = _parse_catalog_list(base_list.resolve(strict=True))
    overlay_catalogs = _parse_catalog_list(overlay_list.resolve(strict=True))
    catalogs = base_catalogs + overlay_catalogs

    seen_role_prefix: dict[tuple[str, str], Path] = {}
    seen_prefix_path: dict[str, Path] = {}
    declared_files: set[Path] = set()
    for role, directory, prefix in base_catalogs + overlay_catalogs:
        expected_manifest_role = "base-catalog" if (role, directory, prefix) in base_catalogs else "overlay-catalog"
        identity = (role, prefix if role != "Z80" else "sonic_cd_z80_step")
        previous = seen_role_prefix.get(identity)
        if previous is not None and previous != directory:
            raise BuildInputError(f"catalog prefix {identity[1]} has multiple directories")
        if previous is not None:
            raise BuildInputError(f"duplicate catalog entry {identity[1]}")
        seen_role_prefix[identity] = directory
        if role != "Z80":
            previous = seen_prefix_path.get(prefix)
            if previous is not None and previous != directory:
                raise BuildInputError(f"catalog prefix {prefix} is ambiguous across CPU roles")
            seen_prefix_path[prefix] = directory
        expected = _expected_catalog_files(role, directory, prefix)
        for file_path in expected:
            if not file_path.is_file():
                raise BuildInputError(f"catalog is missing required source: {file_path}")
            resolved = file_path.resolve(strict=True)
            declared_files.add(resolved)
            record = file_hashes.get(_path_key(resolved))
            if record is None:
                raise BuildInputError(f"catalog source is absent from manifest: {resolved}")
            digest, manifest_role = record
            if manifest_role != expected_manifest_role:
                raise BuildInputError(f"manifest role mismatch for {resolved}: expected {expected_manifest_role}")
            if sha256_file(resolved) != digest:
                raise BuildInputError(f"catalog source hash mismatch: {resolved}")

    # Every manifest-listed source file must belong to exactly one configured
    # catalog; this prevents unreferenced or silently omitted generated sources.
    source_records = {key for key in file_hashes if Path(key).suffix.lower() in {".c", ".h"}}
    declared_keys = {_path_key(path) for path in declared_files}
    if source_records != declared_keys:
        missing = sorted(source_records - declared_keys)
        extra = sorted(declared_keys - source_records)
        raise BuildInputError(f"catalog list/manifest source mismatch; unlisted={missing[:3]} unexpected={extra[:3]}")

    required_base = {("MAIN", "segacd_bios"), ("MAIN", "segacd_main_ram"), ("SUB", "segacd_sub"),
                     ("MAIN", "segacd_boot_main"), ("SUB", "segacd_boot_sub"),
                     ("SUB", "segacd_sub_patch"), ("Z80", "sonic_cd_z80_step")}
    actual_base = {(role, prefix) for role, _, prefix in base_catalogs}
    if not required_base <= actual_base:
        missing = sorted(prefix for _, prefix in required_base - actual_base)
        raise BuildInputError(f"required base catalogs missing: {', '.join(missing)}")

    return {"manifest": manifest, "manifest_path": manifest_path, "catalogs": catalogs,
            "manifest_sha256": sha256_file(manifest_path),
            "base_list_sha256": sha256_file(base_list), "overlay_list_sha256": sha256_file(overlay_list)}


def validate_owned_inputs(bios_path: Path, disc_path: Path) -> dict:
    bios_path = bios_path.resolve(strict=True)
    if not bios_path.is_file() or bios_path.stat().st_size != BIOS_BYTES:
        raise BuildInputError(f"BIOS must be a regular {BIOS_BYTES}-byte file")
    bios_sha = sha256_file(bios_path)
    if bios_sha != BIOS_SHA256:
        raise BuildInputError(f"BIOS SHA-256 is not the qualified revision: {bios_sha}")
    crc = 0
    with bios_path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            crc = zlib.crc32(chunk, crc)
    if crc & 0xFFFFFFFF != BIOS_CRC32:
        raise BuildInputError("BIOS CRC-32 does not match the standalone runner contract")

    disc_path = disc_path.resolve(strict=True)
    if not disc_path.is_file():
        raise BuildInputError("SGCD input is not a regular file")
    extractor_path = Path(__file__).with_name("sgcd_owned_extract.py")
    import importlib.util
    spec = importlib.util.spec_from_file_location("segacd_sgcd_owned_extract", extractor_path)
    if spec is None or spec.loader is None:
        raise BuildInputError("cannot load the SGCD format validator")
    extractor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extractor)
    try:
        image = extractor.SGCD(disc_path)
    except Exception as error:
        raise BuildInputError(f"invalid SGCD image: {error}") from error
    if image.payload_sha256 != DISC_PAYLOAD_SHA256:
        raise BuildInputError(f"SGCD payload SHA-256 is not the qualified revision: {image.payload_sha256}")
    return {"bios_path": bios_path, "bios_sha256": bios_sha, "bios_crc32": f"{crc & 0xFFFFFFFF:08x}",
            "disc_path": disc_path, "disc_sha256": image.image_sha256,
            "disc_payload_sha256": image.payload_sha256, "disc_payload_bytes": image.payload_bytes}


def _cmake_quote(value: str) -> str:
    if any(ch in value for ch in "\r\n\0"):
        raise BuildInputError("invalid newline/NUL in CMake value")
    return value.replace("\\", "/").replace('"', '\\"').replace("$", "\\$")


def write_initial_cache(cache_path: Path, catalogs: list[tuple[str, Path, str]], sdl2_dir: Path,
                        compact_dir: Path | None = None) -> None:
    by_prefix = {prefix: directory for _, directory, prefix in catalogs}
    values = {
        "SEGACD_BUILD_NATIVE_RUNNER": ("BOOL", "ON"),
        "SEGACD_BIOS_PROBE_MODULE_DIR": ("PATH", str(by_prefix["segacd_bios"])),
        "SEGACD_BIOS_PROBE_RAM_MODULE_DIR": ("PATH", str(by_prefix["segacd_main_ram"])),
        "SEGACD_BIOS_PROBE_SUB_MODULE_DIR": ("PATH", str(by_prefix["segacd_sub"])),
        "SEGACD_BIOS_PROBE_BOOT_MAIN_MODULE_DIR": ("PATH", str(by_prefix["segacd_boot_main"])),
        "SEGACD_BIOS_PROBE_BOOT_SUB_MODULE_DIR": ("PATH", str(by_prefix["segacd_boot_sub"])),
        "SEGACD_BIOS_PROBE_SUB_PATCH_MODULE_DIR": ("PATH", str(by_prefix["segacd_sub_patch"])),
        "SEGACD_BIOS_PROBE_Z80_MODULE_DIR": ("PATH", str(by_prefix["sonic_cd_z80_step"])),
        "SDL2_DIR": ("PATH", str(sdl2_dir)),
        "SEGACD_COMPACT_CATALOG_DIR": ("PATH", str(compact_dir) if compact_dir else ""),
    }
    for role in ("MAIN", "SUB"):
        pairs = [f"{directory}|{prefix}" for entry_role, directory, prefix in catalogs
                 if entry_role == role and prefix not in {
                     "segacd_bios", "segacd_main_ram", "segacd_sub", "segacd_boot_main",
                     "segacd_boot_sub", "segacd_sub_patch"}]
        variable = f"SEGACD_BIOS_PROBE_EXTRA_{role}_MODULES"
        values[variable] = ("STRING", ";".join(pairs))
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Generated by tools/segacd/native_catalog_build.py; edit inputs, not this file."]
    lines += [f'set({name} "{_cmake_quote(value)}" CACHE {kind} "" FORCE)'
              for name, (kind, value) in values.items()]
    cache_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def git_identity(directory: Path) -> dict:
    try:
        revision = subprocess.run(["git", "-c", "core.fsmonitor=false", "rev-parse", "HEAD"], cwd=directory,
                                  check=True, capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "-c", "core.fsmonitor=false", "status", "--porcelain", "--ignore-submodules=all"], cwd=directory,
                               check=True, capture_output=True, text=True).stdout.strip() != ""
    except (OSError, subprocess.CalledProcessError) as error:
        raise BuildInputError(f"cannot record Git identity for {directory}: {error}") from error
    return {"path": str(directory.resolve()), "revision": revision, "dirty": dirty}


def run(args: argparse.Namespace) -> dict:
    source_root = Path(args.source_root).resolve(strict=True)
    if not (source_root / "tests" / "segacd" / "CMakeLists.txt").is_file():
        raise BuildInputError("source root does not contain tests/segacd/CMakeLists.txt")
    bundle = validate_catalog_bundle(Path(args.catalog_manifest), Path(args.base_catalogs), Path(args.append_overlays))
    inputs = validate_owned_inputs(Path(args.bios), Path(args.disc))
    build_dir = Path(args.build_dir).resolve(strict=False)
    sdl2_dir = Path(args.sdl2_dir).resolve(strict=True)
    if not sdl2_dir.is_dir():
        raise BuildInputError("SDL2_DIR is not a directory")
    if any(";" in str(p) or '"' in str(p) for _, p, _ in bundle["catalogs"]):
        raise BuildInputError("catalog path contains characters unsupported by CMake list encoding")
    build_dir.mkdir(parents=True, exist_ok=True)
    host_state = build_dir / "host-state"
    host_temp = host_state / "temp"
    local_app_data = host_state / "local-app-data"
    roaming_app_data = host_state / "roaming-app-data"
    cache_home = host_state / "cache"
    for directory in (host_temp, local_app_data, roaming_app_data, cache_home,
                      host_state / "data", host_state / "state", host_state / "config"):
        directory.mkdir(parents=True, exist_ok=True)
    process_env = os.environ.copy()
    process_env.update({"TEMP": str(host_temp), "TMP": str(host_temp), "TMPDIR": str(host_temp),
                        "LOCALAPPDATA": str(local_app_data), "APPDATA": str(roaming_app_data),
                        "XDG_CACHE_HOME": str(cache_home),
                        "XDG_DATA_HOME": str(host_state / "data"),
                        "XDG_STATE_HOME": str(host_state / "state"),
                        "XDG_CONFIG_HOME": str(host_state / "config")})
    cache_path = build_dir / "segacd-native-initial-cache.cmake"
    compact_dir = None
    compact_warning = None
    if getattr(args, "canonicalize_catalogs", False):
        # Reproduce standalone_program.c's priority exactly. This is mechanical
        # registry adoption; the compactor itself knows no game or ROM identity.
        first = {"MAIN": ["segacd_bios", "segacd_main_ram", "segacd_boot_main"],
                 "SUB": ["segacd_sub", "segacd_sub_patch", "segacd_boot_sub"]}
        by_prefix = {prefix: (role, directory, prefix) for role, directory, prefix in bundle["catalogs"]}
        ordered = []
        for role, prefixes in first.items():
            ordered.extend(by_prefix[prefix] for prefix in prefixes)
            ordered.extend(row for row in bundle["catalogs"] if row[0] == role and row[2] not in prefixes)
        compact_dir = build_dir / "compact-native-catalogs"
        ordered_list = build_dir / "compact-native-inputs.txt"
        ordered_list.write_text("".join(f"{role}|{directory}|{prefix}\n" for role, directory, prefix in ordered), encoding="utf-8")
        try:
            subprocess.run([sys.executable, str(source_root / "tools" / "native_catalog_compact.py"),
                            "--catalog-list", str(ordered_list), "--output", str(compact_dir)],
                           check=True, env=process_env)
        except subprocess.CalledProcessError as error:
            # A newer/unknown generator format must still build through the
            # original portable registry, never through partial metadata.
            compact_warning = f"Catalog compaction exited {error.returncode}; retaining original registry"
            print(compact_warning, file=sys.stderr)
            compact_dir = None
    write_initial_cache(cache_path, bundle["catalogs"], sdl2_dir, compact_dir)
    source_git = git_identity(source_root)
    core_git = {}
    for core in ("m68k-recomp-core", "z80-recomp-core"):
        core_git[core] = git_identity(source_root / "external" / core)
    provenance = {
        "schema": "SegaCDNativeSourceBuild1",
        "source_root": str(source_root), "source_git": source_git, "core_git": core_git,
        "catalog_manifest": str(bundle["manifest_path"]),
        "catalog_manifest_sha256": bundle["manifest_sha256"],
        "base_catalog_list_sha256": bundle["base_list_sha256"],
        "append_overlay_list_sha256": bundle["overlay_list_sha256"],
        "catalog_count": len(bundle["catalogs"]), "catalog_source_file_count": sum(
            len(_expected_catalog_files(*row)) for row in bundle["catalogs"]),
        "owned_inputs": {k: (str(v) if isinstance(v, Path) else v) for k, v in inputs.items()},
        "historical_archive_records_ignored": len(bundle["manifest"].get("archives", [])),
        "build_dir": str(build_dir), "initial_cache": str(cache_path),
        "configuration_requested": bool(args.configure or args.build),
        "build_requested": bool(args.build),
        "compact_catalog_manifest": str(compact_dir / "native_catalog_manifest.json") if compact_dir else None,
        "compact_catalog_manifest_sha256": sha256_file(compact_dir / "native_catalog_manifest.json") if compact_dir else None,
        "compact_catalog_warning": compact_warning,
        "limitation": "Consumes an existing generated catalog bundle; BIOS/disc-driven catalog discovery and generation are separate.",
    }
    provenance_path = build_dir / "source-build-provenance.json"
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    if args.configure or args.build:
        cmake = args.cmake or "cmake"
        subprocess.run([cmake, "-S", str(source_root / "tests" / "segacd"), "-B", str(build_dir),
                        "-C", str(cache_path)], check=True, env=process_env)
        provenance["configured"] = True
    if args.build:
        subprocess.run([args.cmake or "cmake", "--build", str(build_dir), "--target", "SonicCD",
                        "--config", args.configuration, "--parallel", str(args.parallel)], check=True, env=process_env)
        provenance["built"] = True
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    return {"build_dir": build_dir, "cache": cache_path, "provenance": provenance_path,
            "catalog_count": len(bundle["catalogs"]), "source_file_count": provenance["catalog_source_file_count"]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--catalog-manifest", required=True, type=Path)
    parser.add_argument("--base-catalogs", required=True, type=Path)
    parser.add_argument("--append-overlays", required=True, type=Path)
    parser.add_argument("--bios", required=True, type=Path)
    parser.add_argument("--disc", required=True, type=Path, help="qualified SGCDIMG1 input")
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--sdl2-dir", required=True, type=Path)
    parser.add_argument("--cmake", help="CMake executable; defaults to cmake on PATH")
    parser.add_argument("--configure", action="store_true", help="configure after validating and writing cache")
    parser.add_argument("--build", action="store_true", help="configure and build SonicCD from source")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--canonicalize-catalogs", action=argparse.BooleanOptionalAction, default=True,
                        help="compact equivalent generated metadata in original priority (default: enabled; unsupported formats retain original registry)")
    args = parser.parse_args(argv)
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    if args.build:
        args.configure = True
    try:
        result = run(args)
    except (BuildInputError, OSError, subprocess.CalledProcessError) as error:
        print(f"native catalog build: {error}", file=sys.stderr)
        return 1
    print(f"Validated {result['catalog_count']} catalogs ({result['source_file_count']} source files).")
    print(f"CMake cache: {result['cache']}")
    print(f"Build provenance: {result['provenance']}")
    if not args.configure:
        print("Catalog bundle validated; configure/build were not requested.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Regenerate guarded native catalogs from qualified owned BIOS/disc inputs.

Recipes contain addresses, input identities and deterministic diagnostic probe
procedures, never copied game instruction bytes. Diagnostic discovery is an
explicit build step; the generated runner retains strict native execution.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import struct
import subprocess
import sys

try:
    from . import m68k_catalog, native_catalog_build, sgcd_owned_extract, z80_native_generate
except ImportError:
    import m68k_catalog
    import native_catalog_build
    import sgcd_owned_extract
    import z80_native_generate


class RecipeError(ValueError):
    pass


def load_recipe(path: Path) -> dict:
    if path.suffix.lower() != ".gz":
        return native_catalog_build.load_json(path)
    try:
        with gzip.open(path, "rb") as stream:
            data = stream.read(128 * 1024 * 1024 + 1)
        if len(data) > 128 * 1024 * 1024:
            raise RecipeError("expanded recipe exceeds the 128 MiB limit")
        result = json.loads(data.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError, EOFError) as error:
        raise RecipeError(f"cannot decode compressed recipe: {error}") from error
    if not isinstance(result, dict):
        raise RecipeError("recipe must be a JSON object")
    return result


BASE_PREFIXES = {
    "segacd_bios", "segacd_main_ram", "segacd_sub", "segacd_boot_main",
    "segacd_boot_sub", "segacd_sub_patch",
}
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]{0,95}\Z")
PROFILE_ID = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}\Z")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_hash(data: bytes, expected: str, label: str) -> None:
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected):
        raise RecipeError(f"missing/invalid SHA-256 for {label}")
    if sha(data) != expected.lower():
        raise RecipeError(f"SHA-256 mismatch for {label}: {sha(data)}")


def relative_path(root: Path, value: str) -> Path:
    if not isinstance(value, str) or not value or Path(value).is_absolute() or PureWindowsPath(value).drive:
        raise RecipeError(f"expected a relative path: {value!r}")
    path = (root / value).resolve()
    if not path.is_relative_to(root.resolve()):
        raise RecipeError(f"path escapes its input directory: {value}")
    return path


def integer(value) -> int:
    if isinstance(value, bool) or not isinstance(value, (str, int)):
        raise RecipeError("address/count must be an integer or integer string")
    try:
        return int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError, OverflowError) as error:
        raise RecipeError(f"invalid integer: {value!r}") from error


def decode_roots(row: dict, image_size: int) -> list[int]:
    base = integer(row["load_base"])
    encoding, values = row.get("roots_encoding"), row.get("roots")
    if not isinstance(values, list):
        raise RecipeError("roots must be an array")
    roots = set()
    if encoding == "ranges":
        for item in values:
            if not isinstance(item, list) or len(item) != 3:
                raise RecipeError("root ranges must contain start, end, step")
            start, end, step = map(integer, item)
            if step != 2 or start >= end or start < base or end > base + image_size or start & 1 or end & 1:
                raise RecipeError("invalid/oversized word-address root range")
            roots.update(range(start, end, step))
    elif encoding == "pc_list":
        roots = {integer(value) for value in values}
    else:
        raise RecipeError(f"unknown root encoding {encoding!r}")
    if (not roots or len(roots) != integer(row["required_entries"]) or
            any(pc & 1 or pc < base or pc + 2 > base + image_size for pc in roots)):
        raise RecipeError(f"invalid/incomplete roots for {row.get('prefix')}")
    return sorted(roots)


def guard_records(module: Path) -> dict[int, tuple[tuple[int, ...], int]]:
    result = {}
    for match in m68k_catalog._MODULE_ENTRY.finditer(module.read_text(encoding="utf-8")):
        pc, count, cycles = int(match[1], 16), int(match[3]), int(match[4])
        words = tuple(int(word.strip(), 16) for word in match[2].split(",") if word.strip())
        if count != len(words) or pc in result:
            raise RecipeError(f"duplicate/malformed guard at {pc:06X} in {module}")
        result[pc] = (words, cycles)
    return result


def guard_digest(records: dict[int, tuple[tuple[int, ...], int]], roots: list[int]) -> str:
    digest = hashlib.sha256()
    for pc in roots:
        if pc not in records:
            raise RecipeError(f"required guarded entry {pc:06X} is absent")
        words, cycles = records[pc]
        digest.update(struct.pack(">IHH", pc, len(words), cycles))
        digest.update(struct.pack(">" + "H" * len(words), *words))
    return digest.hexdigest()


def host_environment(root: Path) -> dict[str, str]:
    root = root.resolve()
    if PureWindowsPath(str(root)).drive.casefold() == "c:":
        raise RecipeError("generated data on C: is refused; choose F: or G:")
    env = {key: value for key, value in os.environ.items() if not key.startswith("SEGACD_PROBE_")}
    for key, suffix in {"TEMP": "temp", "TMP": "temp", "TMPDIR": "temp",
                        "LOCALAPPDATA": "appdata/Local", "APPDATA": "appdata/Roaming",
                        "XDG_CACHE_HOME": "cache", "XDG_DATA_HOME": "data",
                        "XDG_STATE_HOME": "state", "XDG_CONFIG_HOME": "config"}.items():
        path = root / "host-state" / suffix
        path.mkdir(parents=True, exist_ok=True)
        env[key] = str(path)
    env.update({"PYTHONDONTWRITEBYTECODE": "1", "SDL_AUDIODRIVER": "dummy"})
    return env


def capture_profiles(recipe: dict, root: Path, probe: Path, bios: Path, disc: Path,
                     environment: dict[str, str], resume: bool = False,
                     prior_profiles: dict | None = None) -> dict:
    profiles = recipe.get("capture_profiles", [])
    if not isinstance(profiles, list):
        raise RecipeError("capture_profiles must be an ordered array")
    root.mkdir(parents=True, exist_ok=True)
    bios_sha = native_catalog_build.sha256_file(bios)
    disc_sha = native_catalog_build.sha256_file(disc)
    probe_sha = native_catalog_build.sha256_file(probe)
    completed = dict(prior_profiles or {})
    for profile in profiles:
        name = profile["id"]
        if not PROFILE_ID.fullmatch(name) or name in completed:
            raise RecipeError("duplicate/invalid capture profile identifier")
        output = root / name
        env = environment.copy()
        seed = profile.get("backup", {"kind": "fill", "value": 255})
        if seed.get("kind") == "fill":
            value = integer(seed.get("value", 255))
            if not 0 <= value <= 255:
                raise RecipeError("invalid backup fill byte")
            backup = bytes([value]) * 8192
        elif seed.get("kind") == "profile":
            if seed.get("profile") not in completed:
                raise RecipeError("backup dependency must be an earlier capture profile")
            backup = relative_path(root / seed["profile"], seed["relative_file"]).read_bytes()
            checked_hash(backup, seed["sha256"], "captured backup dependency")
            if len(backup) != 8192:
                raise RecipeError("captured backup must be 8192 bytes")
        else:
            raise RecipeError("backup must be generated by a fill or an earlier probe profile")
        stamp = {"profile_spec_sha256": sha(json.dumps(profile, sort_keys=True).encode("utf-8")),
                 "input_backup_sha256": sha(backup), "bios_sha256": bios_sha,
                 "disc_sha256": disc_sha, "probe_sha256": probe_sha}
        if output.exists() and resume:
            previous = native_catalog_build.load_json(output / "capture-provenance.json")
            if any(previous.get(key) != value for key, value in stamp.items()):
                raise RecipeError(f"existing capture profile identity differs: {name}")
            artifacts = previous.get("artifacts", [])
            if not artifacts:
                raise RecipeError(f"existing capture profile has no artifact inventory: {name}")
            for artifact in artifacts:
                path = relative_path(output, artifact["path"])
                if native_catalog_build.sha256_file(path) != artifact["sha256"]:
                    raise RecipeError(f"existing capture artifact was changed: {path}")
            completed[name] = previous
            continue
        output.mkdir(parents=True, exist_ok=False)
        backup_path = output / "input-backup.bin"
        backup_path.write_bytes(backup)
        env["SEGACD_PROBE_BRAM_IMAGE"] = str(backup_path)
        for field, variable in {"pads": "SEGACD_PROBE_PAD_EVENTS",
                                "snapshot_frames": "SEGACD_PROBE_SNAPSHOT_FRAMES",
                                "capture_main_pc": "SEGACD_PROBE_CAPTURE_MAIN_PC"}.items():
            value = profile.get(field)
            if value is not None and value != [] and value != "":
                env[variable] = ",".join(map(str, value)) if isinstance(value, list) else str(value)
        env["SEGACD_PROBE_PC_ROOTS"] = "1"
        if profile.get("override_start"):
            env["SEGACD_PROBE_PAD_EVENTS_OVERRIDE_START"] = "1"
        if profile.get("vector_changes"):
            env["SEGACD_PROBE_VECTOR_CHANGES"] = "1"
        deadline = integer(profile["subclock_deadline"])
        if deadline <= 0:
            raise RecipeError("capture deadline must be positive")
        command = [str(probe), str(bios), str(deadline), str(output), str(disc)]
        if profile.get("start_frame") is not None:
            start_frame = integer(profile["start_frame"])
            if start_frame < 0:
                raise RecipeError("start_frame must be nonnegative or null")
            command.append(str(start_frame))
        with (output / "probe.log").open("wb") as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        completed[name] = {**stamp, "command": command,
                           "environment": {key: value for key, value in env.items() if key.startswith("SEGACD_PROBE_")},
                           "artifacts": [{"path": path.relative_to(output).as_posix(),
                                          "sha256": native_catalog_build.sha256_file(path)}
                                         for path in sorted(output.rglob("*")) if path.is_file()]}
        (output / "capture-provenance.json").write_text(json.dumps(completed[name], indent=2) + "\n")
    return completed


def source_image(source: dict, bios: Path, disc: Path, extracted: Path,
                 inventory: dict, captures: Path, verified_profiles: dict | None = None) -> bytes:
    kind = source.get("kind")
    if kind == "derived_bios_vector":
        setup_source, store_source = source["setup_source"], source["store_source"]
        template_source = source["vector_template_source"]
        for item in (setup_source, store_source, template_source):
            if item.get("kind") != "owned_file" or item.get("file_sha256") != native_catalog_build.BIOS_SHA256:
                raise RecipeError("BIOS vector derivation requires the qualified owned BIOS")
        setup = source_image(setup_source, bios, disc, extracted, inventory, captures)
        store = source_image(store_source, bios, disc, extracted, inventory, captures)
        template = source_image(template_source, bios, disc, extracted, inventory, captures)
        if len(setup) != 10 or len(store) != 6 or len(template) != 2:
            raise RecipeError("invalid BIOS LEA/call/store vector proof length")
        lea = int.from_bytes(setup[:2], "big")
        register = (lea >> 9) & 7
        call_target = integer(setup_source["offset"]) + 8 + int.from_bytes(setup[8:10], "big", signed=True)
        destination = int.from_bytes(store[2:4], "big", signed=True) & 0xFFFFFF
        if ((lea & 0xF1FF) != 0x41F9 or int.from_bytes(setup[6:8], "big") not in {0x6100, 0x4EBA} or
                call_target != integer(store_source["offset"]) or
                int.from_bytes(store[:2], "big") != (0x21C8 | register) or
                int.from_bytes(store[4:6], "big") != 0x4E75 or
                destination != integer(source["source_base"]) + 2 or
                int.from_bytes(template, "big") != 0x4EF9 or integer(source["bytes"]) != 6):
            raise RecipeError("owned BIOS does not establish the LEA/call/store vector assignment")
        image = template + setup[2:6]
        checked_hash(image, source["sha256"], "BIOS-derived original vector")
        return image
    if kind == "derived_vector_header":
        target_source, loader_source = source["target_source"], source["loader_source"]
        template_source = source["vector_template_source"]
        if any(item.get("kind") != "owned_file" for item in (target_source, loader_source, template_source)):
            raise RecipeError("header vector requires owned header, loader and BIOS template sources")
        if (template_source.get("mapping") != "direct-BIOS" and
                template_source.get("file_sha256") != native_catalog_build.BIOS_SHA256):
            raise RecipeError("header vector template must come from the qualified BIOS")
        template = source_image(template_source, bios, disc, extracted, inventory, captures)
        target = source_image(target_source, bios, disc, extracted, inventory, captures)
        loader = source_image(loader_source, bios, disc, extracted, inventory, captures)
        field, vector_pc = integer(target_source["offset"]), integer(source["source_base"])
        if (field, vector_pc) not in {(12, 0xFFFD0C), (16, 0xFFFD06)}:
            raise RecipeError("header field does not select the declared HInt/VInt vector")
        # The owned IPX loader reads the MMD header in Word RAM, tests for a
        # nonzero handler, then writes its pointer into the BIOS JMP operand.
        proof = struct.pack(">HIHHH", 0x2039, 0x200000 + field, 0x6704, 0x21C0, (vector_pc + 2) & 0xFFFF)
        if proof not in loader or len(target) != 4 or not any(target):
            raise RecipeError("owned loader/header does not establish this vector assignment")
        if len(template) != 2 or int.from_bytes(template, "big") != 0x4EF9 or integer(source["bytes"]) != 6:
            raise RecipeError("header-derived JMP requires the owned six-byte vector template")
        image = template + target
        checked_hash(image, source["sha256"], "header-derived original vector")
        return image
    if kind == "derived_vector":
        writer_source, template_source = source["writer_source"], source["vector_template_source"]
        if writer_source.get("kind") != "owned_file" or template_source.get("kind") != "owned_file":
            raise RecipeError("vector derivation requires owned writer and BIOS template sources")
        if (template_source.get("mapping") != "direct-BIOS" and
                template_source.get("file_sha256") != native_catalog_build.BIOS_SHA256):
            raise RecipeError("vector template must come from the qualified owned BIOS")
        writer = source_image(writer_source, bios, disc, extracted, inventory, captures)
        template = source_image(template_source, bios, disc, extracted, inventory, captures)
        if len(template) != 2 or int.from_bytes(template, "big") != 0x4EF9:
            raise RecipeError("owned BIOS does not supply the JMP vector template")
        opcode = int.from_bytes(writer[:2], "big")
        if opcode == 0x21FC and len(writer) == 8:
            destination = int.from_bytes(writer[6:8], "big", signed=True) & 0xFFFFFF
        elif opcode == 0x23FC and len(writer) == 10:
            destination = int.from_bytes(writer[6:10], "big") & 0xFFFFFF
        else:
            raise RecipeError("vector writer is not an immediate longword store to an absolute slot")
        if destination != integer(source["source_base"]) + 2:
            raise RecipeError("vector writer does not target this JMP operand")
        image = template + writer[2:6]
        if integer(source["bytes"]) != 6:
            raise RecipeError("derived JMP vector must contain six bytes")
        checked_hash(image, source["sha256"], "derived original vector")
        return image
    if kind == "owned_file":
        if (source.get("mapping") == "direct-BIOS" or
                source.get("file_sha256") == native_catalog_build.BIOS_SHA256):
            path = bios
        elif source.get("segments"):
            path = disc
        else:
            name = source.get("name")
            matches = [row for row in inventory["files"] if row["iso_path"] == name or
                       Path(row["iso_path"]).name == name]
            if len(matches) != 1:
                raise RecipeError(f"owned program name is missing/ambiguous: {name}")
            path = relative_path(extracted, matches[0]["output_path"])
    elif kind == "probe_capture":
        profile = source.get("profile", "")
        if not PROFILE_ID.fullmatch(profile):
            raise RecipeError("invalid capture source profile")
        if verified_profiles is None or profile not in verified_profiles:
            raise RecipeError(f"capture source has no verified current profile: {profile}")
        path = relative_path(captures / profile, source["relative_file"])
        artifacts = verified_profiles[profile].get("artifacts", [])
        if not any(relative_path(captures / profile, artifact["path"]) == path and
                   artifact["sha256"].lower() == source["file_sha256"].lower() for artifact in artifacts):
            raise RecipeError(f"capture source is not a verified profile artifact: {path}")
    else:
        raise RecipeError(f"unsupported source kind {kind!r}")
    if source.get("segments"):
        if kind != "owned_file" or path != disc:
            raise RecipeError("segmented reads require the qualified disc")
        pieces = []
        with path.open("rb") as stream:
            for segment in source["segments"]:
                offset, count = integer(segment["raw_offset"]), integer(segment["bytes"])
                if offset < 0 or not 0 < count <= 2352:
                    raise RecipeError("invalid disc segment")
                stream.seek(offset)
                data = stream.read(count)
                if len(data) != count:
                    raise RecipeError("truncated disc segment")
                pieces.append(data)
        image = b"".join(pieces)
        # Container hash is checked once before extraction; segment hashes
        # qualify this mapping independently of a cache file's name.
    else:
        data = path.read_bytes()
        checked_hash(data, source["file_sha256"], str(path))
        offset, count = integer(source.get("offset", 0)), integer(source["bytes"])
        if offset < 0 or count <= 0 or offset + count > len(data):
            raise RecipeError(f"source slice outside {path}")
        image = data[offset:offset + count]
    if len(image) != integer(source["bytes"]) or not image or len(image) & 1:
        raise RecipeError("source image must have the declared even byte length")
    checked_hash(image, source["sha256"], "mapped source image")
    return image


def generate_image(row: dict, image: bytes, output: Path, generator: Path,
                   environment: dict[str, str], resume: bool = False) -> dict:
    cpu, prefix, base = row["cpu"], row["prefix"], integer(row["load_base"])
    if cpu not in {"MAIN", "SUB"} or not IDENTIFIER.fullmatch(prefix):
        raise RecipeError("invalid native image CPU/prefix")
    if base < 0 or base & 1 or base + len(image) > 0x1000000:
        raise RecipeError("mapped image lies outside word-aligned 24-bit address space")
    roots = decode_roots(row, len(image))
    roots_sha = sha("".join(f"0x{pc:06X}\n" for pc in roots).encode("ascii"))
    if output.exists() and resume:
        old = native_catalog_build.load_json(output / "generation-provenance.json")
        if (old.get("source_sha256") != sha(image) or old.get("cpu") != cpu or
                old.get("prefix") != prefix or old.get("required_entries") != len(roots) or
                old.get("load_base") != base or old.get("roots_sha256") != roots_sha or
                old.get("required_guard_sha256") != row["guard_sha256"].lower() or
                old.get("generator_sha256") != native_catalog_build.sha256_file(generator)):
            raise RecipeError(f"existing generated input identity differs: {prefix}")
        files = old.get("catalog_files", [])
        expected = native_catalog_build._expected_catalog_files(cpu, output / "catalog", prefix)
        if {relative_path(output / "catalog", item["path"]) for item in files} != expected:
            raise RecipeError(f"existing generated file inventory is incomplete: {prefix}")
        for item in files:
            path = relative_path(output / "catalog", item["path"])
            if native_catalog_build.sha256_file(path) != item["sha256"]:
                raise RecipeError(f"existing generated source was changed: {path}")
        if guard_digest(guard_records(output / "catalog" / f"{prefix}_module.c"), roots) != row["guard_sha256"].lower():
            raise RecipeError(f"existing required guard semantics differ: {prefix}")
        return old
    output.mkdir(parents=True, exist_ok=False)
    image_path, roots_path = output / "owned-image.bin", output / "roots.txt"
    image_path.write_bytes(image)
    roots_path.write_text("".join(f"0x{pc:06X}\n" for pc in roots), encoding="ascii")
    catalog = output / "catalog"
    catalog.mkdir()
    command = [str(generator), str(image_path), str(catalog), prefix, hex(base),
               "--entries-file", str(roots_path)]
    with (output / "generator.log").open("wb") as log:
        subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    rows = m68k_catalog.read_coverage(catalog / f"{prefix}_coverage.csv")
    failures = m68k_catalog.validate_coverage_rows(image, base, rows)
    audit = m68k_catalog.audit_module(image, base, catalog / f"{prefix}_module.c", rows, prefix)
    if failures or audit["failures"] or any(row["kind"] == "unsupported" for row in rows):
        raise RecipeError(f"generated catalog audit failed: {prefix}")
    records = guard_records(catalog / f"{prefix}_module.c")
    digest = guard_digest(records, roots)
    if digest != row["guard_sha256"].lower():
        raise RecipeError(f"qualified guard semantics changed: {prefix} ({digest})")
    result = {"cpu": cpu, "prefix": prefix, "catalog": str(catalog),
              "load_base": base, "roots_sha256": roots_sha,
              "source_sha256": sha(image), "required_entries": len(roots),
              "required_guard_sha256": digest, "emitted_entries": len(records),
              "additional_graph_entries": len(records) - len(roots), "command": command,
              "generator_sha256": native_catalog_build.sha256_file(generator),
              "catalog_files": [{"path": path.name, "sha256": native_catalog_build.sha256_file(path)}
                                for path in sorted(native_catalog_build._expected_catalog_files(cpu, catalog, prefix))]}
    (output / "generation-provenance.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def write_bundle(output: Path, records: list[dict], z80: dict) -> dict:
    """Write the source-only bundle consumed by native_catalog_build.py."""
    c_path = Path(z80["generated"]["c"]["path"]).resolve(strict=True)
    header = Path(z80["generated"]["header"]["path"]).resolve(strict=True)
    if c_path.parent != header.parent:
        raise RecipeError("Z80 C/header must share a generated directory")
    for path, item in ((c_path, z80["generated"]["c"]), (header, z80["generated"]["header"])):
        if native_catalog_build.sha256_file(path) != item["sha256"]:
            raise RecipeError(f"Z80 generated source changed after its audit: {path}")
    base, overlays, base_files, overlay_files = [], [], [], []
    for row in records:
        cpu, prefix, directory = row["cpu"], row["prefix"], Path(row["catalog"])
        is_base = prefix in BASE_PREFIXES
        (base if is_base else overlays).append(f"{cpu}|{directory.as_posix()}|{prefix}")
        file_list = base_files if is_base else overlay_files
        for path in sorted(native_catalog_build._expected_catalog_files(cpu, directory, prefix)):
            file_list.append({"Path": str(path.resolve(strict=True)),
                              "Sha256": native_catalog_build.sha256_file(path),
                              "Role": "base-catalog" if is_base else "overlay-catalog"})
    base.append(f"Z80|{c_path.parent.as_posix()}|sonic_cd_z80_step")
    for path in (c_path, header):
        base_files.append({"Path": str(path), "Sha256": native_catalog_build.sha256_file(path),
                           "Role": "base-catalog"})
    base_path, overlay_path = output / "base-catalogs.txt", output / "append-overlays.txt"
    base_path.write_text("\n".join(base) + "\n", encoding="utf-8")
    overlay_path.write_text("\n".join(overlays) + "\n", encoding="utf-8")
    manifest = output / "catalog-manifest.json"
    manifest.write_text(json.dumps({"schema": "SegaCDOwnedNativeCatalogBundle1",
                                   "base_catalogs": base_files, "overlay_catalogs": overlay_files,
                                   "archives": [], "generation_provenance": "generation-provenance.json"},
                                  indent=2) + "\n", encoding="utf-8")
    bundle = native_catalog_build.validate_catalog_bundle(manifest, base_path, overlay_path)
    return {"manifest": str(manifest), "base_catalogs": str(base_path), "append_overlays": str(overlay_path),
            "catalog_count": len(bundle["catalogs"]), "source_file_count": len(base_files) + len(overlay_files)}


def build_z80_capture_probe(source_root: Path, build_dir: Path, catalog_dir: Path,
                            cmake: str, environment: dict, parallel: int) -> Path:
    """Explicit discovery tool: diagnostic M68K, strict native Z80 only."""
    build_dir.mkdir(parents=True, exist_ok=True)
    configure = [cmake, "-S", str(source_root / "tests" / "segacd"), "-B", str(build_dir),
                 f"-DSEGACD_BIOS_PROBE_Z80_MODULE_DIR={catalog_dir}",
                 "-DSEGACD_BIOS_PROBE_MODULE_DIR=", "-DSEGACD_BUILD_NATIVE_RUNNER=OFF"]
    build = [cmake, "--build", str(build_dir), "--config", "Release",
             "--target", "segacd_bios_floor_probe", "--parallel", str(parallel)]
    with (build_dir / "build.log").open("wb") as log:
        subprocess.run(configure, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(build, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [build_dir / "Release" / ("segacd_bios_floor_probe" + suffix),
                  build_dir / ("segacd_bios_floor_probe" + suffix)]
    matches = [path for path in candidates if path.is_file()]
    if len(matches) != 1:
        raise RecipeError("could not identify the newly built native-Z80 diagnostic probe")
    probe = matches[0]
    (build_dir / "build-provenance.json").write_text(json.dumps({
        "configure": configure, "build": build, "probe": str(probe),
        "probe_sha256": native_catalog_build.sha256_file(probe),
        "z80_c_sha256": native_catalog_build.sha256_file(catalog_dir / "sonic_cd_z80_step.c"),
        "diagnostic_m68k_only": True, "z80_interpreter_fallback": False}, indent=2) + "\n")
    return probe


def generate_z80_stages(recipe: dict, output: Path, profiles: dict, bios: Path, disc: Path,
                         extracted: Path, inventory: dict, args, environment: dict) -> tuple[dict, dict]:
    stages = recipe.get("z80_stages")
    if not isinstance(stages, list) or not stages:
        raise RecipeError("z80_stages must be a nonempty ordered array")
    if args.z80_generator is None:
        raise RecipeError("the full recipe requires --z80-generator")
    generator = args.z80_generator.resolve(strict=True)
    generator_sha = native_catalog_build.sha256_file(generator)
    previous, result = None, {}
    used_ids = set()
    for stage in stages:
        name = stage["id"]
        if not PROFILE_ID.fullmatch(name) or name in used_ids:
            raise RecipeError("invalid/duplicate Z80 bootstrap stage")
        used_ids.add(name)
        if stage.get("capture_profiles"):
            if previous is None or args.source_root is None:
                raise RecipeError("Z80 discovery capture needs a prior native catalog and --source-root")
            probe = build_z80_capture_probe(args.source_root.resolve(strict=True), output / "probes" / name,
                                            Path(previous["generated"]["c"]["path"]).parent,
                                            args.cmake, environment, args.parallel)
            profiles = capture_profiles(stage, output / "captures", probe, bios, disc, environment,
                                         args.resume, profiles)
        rows = []
        for source in stage["sources"]:
            if source.get("kind") != "probe_capture" or integer(source.get("offset", 0)) != 0:
                raise RecipeError("Z80 source must be an entire qualified probe RAM capture")
            image = source_image(source, bios, disc, extracted, inventory, output / "captures", profiles)
            if len(image) != z80_native_generate.SNAPSHOT_BYTES:
                raise RecipeError("Z80 source capture must contain 8192 bytes")
            rows.append({"path": str(relative_path(output / "captures" / source["profile"], source["relative_file"])),
                         "sha256": sha(image), "profile": source["profile"],
                         "capture_profile_spec_sha256": profiles[source["profile"]]["profile_spec_sha256"]})
        rows = z80_native_generate._source_rows(rows)
        destination = output / "z80" / name
        if destination.exists() and args.resume:
            manifest = native_catalog_build.load_json(destination / "manifest.json")
            old_rows = [row["source"] for row in manifest["source_rows"]]
            if old_rows != rows or manifest["generator"]["sha256"] != generator_sha:
                raise RecipeError(f"existing Z80 generator inputs changed: {name}")
            for key in ("c", "header"):
                item = manifest["generated"][key]
                expected_path = destination / "seed" / "generated" / ("sonic_cd_z80_step.c" if key == "c" else "sonic_cd_z80_step.h")
                if Path(item["path"]).resolve() != expected_path.resolve():
                    raise RecipeError(f"existing Z80 generated output path differs: {name}/{key}")
                if native_catalog_build.sha256_file(Path(item["path"])) != item["sha256"]:
                    raise RecipeError(f"existing Z80 generated source changed: {name}/{key}")
            expected_bytes = [set() for _ in range(z80_native_generate.MIRRORED_BYTES)]
            for source in rows:
                data = Path(source["path"]).read_bytes() * 2
                for pc, value in enumerate(data):
                    expected_bytes[pc].add(value)
            z80_native_generate._audit_generated_c(Path(manifest["generated"]["c"]["path"]), expected_bytes)
            if b"void sonic_cd_z80_step(void);" not in Path(manifest["generated"]["header"]["path"]).read_bytes():
                raise RecipeError("existing Z80 header lost its entry declaration")
        else:
            manifest = z80_native_generate.generate(rows, generator, destination, environment)
        result[name] = manifest
        previous = manifest
    return previous, {"stages": result, "profiles": profiles}


def run(args) -> dict:
    recipe_path = args.recipe.resolve(strict=True)
    recipe = load_recipe(recipe_path)
    if recipe.get("schema") != "SegaCDOwnedNativeRecipe1":
        raise RecipeError("recipe has an unsupported schema or unresolved sources")
    if args.phase == "all" and recipe.get("z80_stages"):
        if args.z80_generator is None or args.source_root is None:
            raise RecipeError("the complete bootstrap recipe requires --z80-generator and --source-root")
        if not args.z80_generator.is_file() or not (args.source_root / "tests" / "segacd" / "CMakeLists.txt").is_file():
            raise RecipeError("Z80 frontend or diagnostic-probe source checkout is missing")
    output = args.output_dir.resolve()
    if output.exists() and not args.resume:
        raise RecipeError("output directory must be new; prior runs are preserved")
    environment = host_environment(output)
    inputs = native_catalog_build.validate_owned_inputs(args.bios, args.disc)
    bios, disc = inputs["bios_path"], inputs["disc_path"]
    generator, probe = args.generator.resolve(strict=True), args.probe.resolve(strict=True)
    extracted = output / "owned-programs"
    if extracted.exists() and args.resume:
        inventory = native_catalog_build.load_json(extracted / "owned-program-manifest.json")
        if inventory.get("source_sgcd_sha256") != inputs["disc_sha256"]:
            raise RecipeError("existing extracted program inventory belongs to a different disc")
    else:
        inventory = sgcd_owned_extract.extract_programs(disc, extracted)
    images = recipe.get("images")
    if not isinstance(images, list) or not images:
        raise RecipeError("recipe has no native images")
    if len({row.get("prefix") for row in images}) != len(images):
        raise RecipeError("duplicate native image prefix")
    if args.phase == "owned":
        images = [row for row in images if row.get("source") and
                  row["source"].get("kind") in {"owned_file", "derived_vector", "derived_vector_header", "derived_bios_vector"}]
    elif any(not row.get("source") for row in images) or recipe.get("unresolved"):
        raise RecipeError("complete generation requires every native source to be resolved")
    profiles = (capture_profiles(recipe, output / "captures", probe, bios, disc, environment, args.resume)
                if args.phase == "all" else {})
    records = []
    for row in images:
        image = source_image(row["source"], bios, disc, extracted, inventory, output / "captures", profiles)
        records.append(generate_image(row, image, output / "m68k" / row["prefix"], generator, environment, args.resume))
        print(f"Generated {row['prefix']} ({len(records)}/{len(images)})", flush=True)
    report = {"schema": "SegaCDOwnedNativeGeneration1", "recipe_sha256": native_catalog_build.sha256_file(recipe_path),
              "owned_inputs": {key: str(value) if isinstance(value, Path) else value for key, value in inputs.items()},
              "phase": args.phase, "complete_m68k_recipe": args.phase == "all",
              "capture_profiles": profiles, "m68k_catalogs": records,
              "diagnostic_capture_is_not_runtime_fallback": True}
    if args.phase == "all" and recipe.get("z80_stages"):
        z80, bootstrap = generate_z80_stages(recipe, output, profiles, bios, disc, extracted, inventory,
                                            args, environment)
        report["z80_bootstrap"] = bootstrap
        report["bundle"] = write_bundle(output, records, z80)
    (output / "generation-provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("recipe", "bios", "disc", "generator", "probe", "output-dir"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--phase", choices=("owned", "all"), default="all",
                        help="owned generates disc/BIOS images first; all also executes diagnostic captures")
    parser.add_argument("--resume", action="store_true", help="reuse only hash-verified generated catalogs")
    parser.add_argument("--z80-generator", type=Path, help="SmsRecomp flat-step frontend for full bootstrap recipes")
    parser.add_argument("--source-root", type=Path, help="source checkout used to build native-Z80 diagnostic probes")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--parallel", type=int, default=2)
    args = parser.parse_args(argv)
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    try:
        result = run(args)
    except (RecipeError, native_catalog_build.BuildInputError, m68k_catalog.CatalogError,
            z80_native_generate.GenerationError, OSError, KeyError, subprocess.SubprocessError) as error:
        print(f"owned native generation: {error}", file=sys.stderr)
        return 1
    print(f"Qualified {len(result['m68k_catalogs'])} M68K catalogs from owned inputs.")
    if result.get("bundle"):
        bundle = result["bundle"]
        print(f"Source bundle: {bundle['catalog_count']} catalogs, {bundle['source_file_count']} C/header files.")
        print(f"Build manifest: {bundle['manifest']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

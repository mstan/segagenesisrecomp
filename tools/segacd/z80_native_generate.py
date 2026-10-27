"""Build a strict flat-step Z80 catalog from caller-qualified RAM snapshots.

This tool verifies input bytes and preserves their provenance. It does not
establish that arbitrary snapshots belong to an owned BIOS/disc or that they
were captured from the target game; callers must qualify that separately.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Mapping, Sequence

SNAPSHOT_BYTES = 0x2000
MIRRORED_BYTES = 0x4000
MAX_VARIANTS = 8
OUTPUT_PREFIX = "sonic_cd_z80"


class GenerationError(RuntimeError):
    """Generation or generated-output contract failed."""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _source_rows(rows: Sequence[Mapping[str, object]]) -> list[dict[str, object]]:
    if not rows:
        raise ValueError("at least one source row is required")
    if len(rows) > MAX_VARIANTS + 1:
        raise ValueError(f"flat-step accepts one base plus at most {MAX_VARIANTS} variants")
    result: list[dict[str, object]] = []
    for index, row in enumerate(rows):
        if not isinstance(row, Mapping):
            raise ValueError(f"source row {index} must be a mapping")
        if "path" not in row or "sha256" not in row:
            raise ValueError(f"source row {index} requires path and sha256")
        path = Path(os.fspath(row["path"])).expanduser().resolve(strict=True)
        if not path.is_file():
            raise ValueError(f"source row {index} is not a file: {path}")
        expected = str(row["sha256"]).lower()
        if not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise ValueError(f"source row {index} sha256 must contain 64 hexadecimal characters")
        data = path.read_bytes()
        if len(data) != SNAPSHOT_BYTES:
            raise ValueError(f"source row {index} must be exactly 8192 bytes, got {len(data)}: {path}")
        actual = _sha256(data)
        if actual != expected:
            raise ValueError(f"source row {index} SHA-256 mismatch: expected {expected}, got {actual}: {path}")
        clean = dict(row)
        clean["path"] = str(path)
        clean["sha256"] = actual
        clean["bytes"] = len(data)
        result.append(clean)
    return result


def _child_environment(environment: Mapping[str, str] | None, scratch: Path) -> dict[str, str]:
    child = os.environ.copy()
    supplied = {str(k): str(v) for k, v in (environment or {}).items()}
    child.update(supplied)
    scratch.mkdir(parents=True, exist_ok=True)
    scratch_value = str(scratch.resolve())
    # Keep generated scratch inside the caller's output unless overridden.
    # Any drive is allowed; callers own free-space checks and cleanup.
    scratch_keys = (
        "TEMP",
        "TMP",
        "TMPDIR",
        "APPDATA",
        "LOCALAPPDATA",
        "XDG_CACHE_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "XDG_CONFIG_HOME",
    )
    for key in scratch_keys:
        value = supplied.get(key, scratch_value)
        child[key] = value
    return child


def _audit_generated_c(path: Path, expected_pc_bytes: Sequence[set[int]] | None = None) -> dict[str, object]:
    text = path.read_text(encoding="utf-8", errors="strict")
    # Strip comments and literals before auditing so they cannot masquerade as
    # executable cases, byte guards, miss calls, or forbidden fallback calls.
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)
    code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', "", code)
    case_values = [int(value, 16) for value in re.findall(r"^\s*case\s+0x([0-9A-Fa-f]{4})\s*:", code, re.MULTILINE)]
    case_pcs = set(case_values)
    expected_pcs = set(range(MIRRORED_BYTES))
    if len(case_values) != MIRRORED_BYTES or case_pcs != expected_pcs:
        missing = sorted(expected_pcs - case_pcs)
        extra = sorted(case_pcs - expected_pcs)
        raise GenerationError(f"generated catalog does not contain exactly 0x4000 PCs (missing={missing[:8]}, extra={extra[:8]})")
    miss_values = [int(value, 16) for value in re.findall(r"sms_dispatch_miss\(0x([0-9A-Fa-f]{4})\)", code)]
    miss_pcs = set(miss_values)
    if len(miss_values) != MIRRORED_BYTES or miss_pcs != expected_pcs:
        missing = sorted(expected_pcs - miss_pcs)
        extra = sorted(miss_pcs - expected_pcs)
        raise GenerationError(f"generated catalog lacks a strict miss for every PC (missing={missing[:8]}, extra={extra[:8]})")
    if expected_pc_bytes is not None:
        if len(expected_pc_bytes) != MIRRORED_BYTES:
            raise GenerationError("expected byte-guard map must contain exactly 0x4000 PCs")
        case_starts = list(re.finditer(r"^\s*case\s+0x([0-9A-Fa-f]{4})\s*:", code, re.MULTILINE))
        for index, match in enumerate(case_starts):
            pc = int(match.group(1), 16)
            block_end = case_starts[index + 1].start() if index + 1 < len(case_starts) else len(code)
            block = code[match.end():block_end]
            guards = {
                int(value, 16)
                for address, value in re.findall(
                    r"sms_read8\(0x([0-9A-Fa-f]{4})\)\s*==\s*0x([0-9A-Fa-f]{2})",
                    block,
                    re.IGNORECASE,
                )
                if int(address, 16) == pc
            }
            required = expected_pc_bytes[pc]
            missing_guards = required - guards
            if not required or missing_guards:
                missing_text = ",".join(f"{value:02X}" for value in sorted(missing_guards))
                raise GenerationError(f"PC 0x{pc:04X} lacks input-byte native guards (missing={missing_text or 'all'})")
    if "interpreter" in code.lower() or "fallback" in code.lower():
        raise GenerationError("generated catalog contains an interpreter/fallback call")
    if "switch (s->pc)" not in code:
        raise GenerationError("generated catalog is missing the flat PC dispatch")
    crc_match = re.search(r"sonic_cd_z80_code_crc32\s*=\s*0x([0-9A-Fa-f]{8})", code)
    size_match = re.search(r"sonic_cd_z80_code_size\s*=\s*0x([0-9A-Fa-f]+)", code)
    if not crc_match or not size_match or int(size_match.group(1), 16) != MIRRORED_BYTES:
        raise GenerationError("generated catalog code size/CRC metadata is missing or not 16KiB")
    return {
        "bytes": path.stat().st_size,
        "sha256": _sha256_file(path),
        "pc_case_count": len(case_pcs),
        "strict_miss_pc_count": len(miss_pcs),
        "input_guarded_pc_count": MIRRORED_BYTES if expected_pc_bytes is not None else 0,
        "code_crc32": crc_match.group(1).upper(),
        "code_size": MIRRORED_BYTES,
        "no_interpreter_or_fallback_calls": True,
    }


def generate(
    source_rows: Sequence[Mapping[str, object]],
    generator: os.PathLike[str] | str,
    output: os.PathLike[str] | str,
    environment: Mapping[str, str] | None = None,
) -> dict[str, object]:
    """Mirror 8KiB source snapshots and invoke SmsRecomp flat-step.

    `source_rows` are caller-provided provenance records, each containing
    `path` and `sha256`. The function validates exact 8KiB length and hash,
    mirrors each image to 16KiB, invokes the pinned caller-selected generator,
    then audits all generated PCs and strict miss branches. This byte-level
    process does not itself certify ownership or capture provenance.
    """
    rows = _source_rows(source_rows)
    generator_path = Path(generator).expanduser().resolve(strict=True)
    if not generator_path.is_file():
        raise ValueError(f"generator is not a file: {generator_path}")
    output_dir = Path(output).expanduser().resolve()
    if output_dir.exists() and any(output_dir.iterdir()):
        raise ValueError(f"output directory must be new or empty: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)
    seed_dir = output_dir / "seed"
    generated_dir = seed_dir / "generated"
    seed_dir.mkdir(exist_ok=True)
    if any(seed_dir.iterdir()):
        raise ValueError("seed/generated output subdirectories must be empty")

    mirrored_rows: list[dict[str, object]] = []
    variant_paths: list[Path] = []
    for index, row in enumerate(rows):
        source = Path(str(row["path"]))
        raw = source.read_bytes()
        mirrored = raw + raw
        dest = seed_dir / f"image-{index}.bin"
        dest.write_bytes(mirrored)
        variant_paths.append(dest)
        mirrored_rows.append({
            "index": index,
            "role": "base" if index == 0 else "variant",
            "source": dict(row),
            "mirrored_path": str(dest),
            "mirrored_bytes": len(mirrored),
            "mirrored_sha256": _sha256(mirrored),
            "mapping": "duplicate the 8KiB RAM image into both halves of the 16KiB logical window",
        })
    game_toml = seed_dir / "game.toml"
    game_toml.write_text(
        f'[game]\noutput_prefix = "{OUTPUT_PREFIX}"\nplatform = "sms"\nrom = "{variant_paths[0].name}"\n',
        encoding="utf-8",
        newline="\n",
    )
    generator_command = [sys.executable, str(generator_path)] if generator_path.suffix.lower() == ".py" else [str(generator_path)]
    command = [*generator_command, "--game", str(game_toml), "--flat-step"]
    for variant in variant_paths[1:]:
        command.extend(["--flat-step-variant", str(variant)])
    child_env = _child_environment(environment, output_dir / ".process-data")
    result = subprocess.run(command, cwd=seed_dir, env=child_env, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    log_path = output_dir / "generation.log"
    log_path.write_text(result.stdout + ("\n--- stderr ---\n" + result.stderr if result.stderr else ""), encoding="utf-8", newline="\n")
    if result.returncode != 0:
        raise GenerationError(f"SmsRecomp failed with exit code {result.returncode}; see {log_path}")
    c_path = generated_dir / f"{OUTPUT_PREFIX}_step.c"
    h_path = generated_dir / f"{OUTPUT_PREFIX}_step.h"
    if not c_path.is_file() or not h_path.is_file():
        raise GenerationError("SmsRecomp did not emit the expected C and header files")
    expected_pc_bytes = [set() for _ in range(MIRRORED_BYTES)]
    for row in mirrored_rows:
        image = Path(str(row["mirrored_path"])).read_bytes()
        for pc, value in enumerate(image):
            expected_pc_bytes[pc].add(value)
    c_audit = _audit_generated_c(c_path, expected_pc_bytes)
    h_bytes = h_path.read_bytes()
    if b"void sonic_cd_z80_step(void);" not in h_bytes:
        raise GenerationError("generated header does not declare sonic_cd_z80_step")
    manifest: dict[str, object] = {
        "schema": "SegaCDZ80NativeCatalogGeneration1",
        "qualification_note": "Input hash/size validation only; source_rows provenance is caller supplied and must be independently qualified.",
        "generator": {"path": str(generator_path), "sha256": _sha256_file(generator_path)},
        "source_rows": mirrored_rows,
        "source_count": len(rows),
        "variant_count": len(rows) - 1,
        "output_contract": {
            "logical_image_bytes": MIRRORED_BYTES,
            "pc_case_count": MIRRORED_BYTES,
            "strict_miss_pc_count": MIRRORED_BYTES,
            "input_guarded_pc_count": c_audit["input_guarded_pc_count"],
            "every_input_byte_variant_guarded": True,
            "interpreter_fallback": False,
        },
        "generated": {
            "c": {"path": str(c_path), **c_audit},
            "header": {"path": str(h_path), "bytes": len(h_bytes), "sha256": _sha256(h_bytes)},
        },
        "command": command,
        "working_directory": str(seed_dir),
        "environment_overrides": sorted((environment or {}).keys()),
        "stdout_tail": result.stdout[-12000:],
        "stderr_tail": result.stderr[-12000:],
        "log": str(log_path),
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest







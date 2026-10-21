#!/usr/bin/env python3
"""Build a guarded MC68000 catalog using explicit byte-index dispatch closure.

This tool performs static analysis over one caller-provided image. It emits
candidate AOT roots, not runtime fallbacks or proof that a target is a valid
game state. Any baseline coverage used to omit callbacks must remain an
explicit dependency of the caller's build.
"""

from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Iterable
import zlib


class CatalogError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def parse_roots(path: Path | None, direct: Iterable[str], image_base: int,
                image_size: int) -> set[int]:
    roots: set[int] = set()
    tokens = list(direct)
    if path is not None:
        try:
            content = path.read_text(encoding="ascii")
        except (OSError, UnicodeError) as exc:
            raise CatalogError(f"cannot read roots file {path}: {exc}") from exc
        tokens.extend(content.split())
    for token in tokens:
        try:
            address = int(token, 0)
        except ValueError as exc:
            raise CatalogError(f"invalid root address {token!r}") from exc
        if address < image_base or address & 1 or address + 1 >= image_base + image_size:
            raise CatalogError(f"root {token!r} is odd or outside the mapped image")
        roots.add(address)
    if not roots:
        raise CatalogError("at least one entry/root is required")
    return roots


def signed8(value: int) -> int:
    return value - 0x100 if value & 0x80 else value


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def brief_data_word_index(extension: int) -> int | None:
    """Return Dn for a 68000 brief Dn.W extension, else None.

    Bit 15 selects A/D register; bits 11..8 encode size, scale, and reserved
    fields. Only Dn.W with no scale/reserved flags is accepted.
    """
    if extension & 0x8F00:
        return None
    return (extension >> 12) & 7


def _word_at(image: bytes, image_base: int, address: int) -> int | None:
    offset = address - image_base
    if offset < 0 or offset + 2 > len(image):
        return None
    return int.from_bytes(image[offset:offset + 2], "big")


def analyze_dispatches(image: bytes, image_base: int,
                       coverage_rows: list[dict[str, str]]) -> tuple[list[dict], list[dict], set[int]]:
    """Find exact byte-index table dispatches among reachable coverage rows."""
    qualified: list[dict] = []
    unqualified: list[dict] = []
    targets: set[int] = set()
    image_end = image_base + len(image)

    for row in coverage_rows:
        try:
            pc = int(row["pc"], 16)
        except (KeyError, ValueError) as exc:
            raise CatalogError(f"malformed generator coverage row: {row!r}") from exc
        opcode = _word_at(image, image_base, pc)
        if opcode is None or (opcode & 0xF1FF) != 0x303B:  # MOVE.W d8(PC,Dn.W),Dn
            continue
        move_ext = _word_at(image, image_base, pc + 2)
        transfer_opcode = _word_at(image, image_base, pc + 4)
        transfer_ext = _word_at(image, image_base, pc + 6)
        move_index = brief_data_word_index(move_ext) if move_ext is not None else None
        transfer_index = brief_data_word_index(transfer_ext) if transfer_ext is not None else None
        destination = (opcode >> 9) & 7
        moveq = _word_at(image, image_base, pc - 6)
        moveb = _word_at(image, image_base, pc - 4)
        byte_disp = _word_at(image, image_base, pc - 2)

        moveq_ok = move_index is not None and moveq == (0x7000 | (move_index << 9))
        moveb_ok = (move_index is not None and
                    moveb == (0x1028 | (move_index << 9)) and byte_disp is not None)
        transfer_ok = transfer_opcode in (0x4EBB, 0x4EFB)
        register_ok = move_index is not None and transfer_index == destination
        # The effective PC base for each indexed displacement is the address
        # of its extension word; only the signed low byte contributes.
        read_base = pc + 2 + signed8(move_ext & 0xFF) if move_ext is not None else 0
        branch_base = pc + 6 + signed8(transfer_ext & 0xFF) if transfer_ext is not None else 0
        same_base = move_ext is not None and transfer_ext is not None and read_base == branch_base
        checks = ((moveq_ok, "missing MOVEQ #0,Dindex"),
                  (moveb_ok, "missing MOVE.B disp(A0),same Dindex"),
                  (transfer_ok, "no adjacent indexed JSR/JMP"),
                  (register_ok, "index/destination register mismatch or invalid Dn.W extension"),
                  (same_base, "PC-relative table bases differ"),
                  (move_ext is None or not (read_base & 1),
                   "odd PC-relative table base is outside this even-index closure model"))
        if not all(ok for ok, _ in checks):
            unqualified.append({
                "pc": f"{pc:06X}", "opcode": f"{opcode:04X}",
                "move_index_extension": None if move_ext is None else f"{move_ext:04X}",
                "move_index_reg": move_index, "move_destination_reg": f"D{destination}",
                "transfer_opcode": None if transfer_opcode is None else f"{transfer_opcode:04X}",
                "transfer_extension": None if transfer_ext is None else f"{transfer_ext:04X}",
                "transfer_index_reg": transfer_index,
                "move_table_base": None if move_ext is None else f"{read_base:06X}",
                "transfer_table_base": None if transfer_ext is None else f"{branch_base:06X}",
                "state_load_words": [None if v is None else f"{v:04X}" for v in (moveq, moveb, byte_disp)],
                "reasons": [reason for ok, reason in checks if not ok],
            })
            continue

        entry = {
            "pc": f"{pc:06X}", "dispatch_opcode": f"{opcode:04X}",
            "state_index_register": f"D{move_index}",
            "state_byte_load_words": [f"{moveq:04X}", f"{moveb:04X}", f"{byte_disp:04X}"],
            "state_byte_displacement": signed16(byte_disp),
            "indexed_move_extension": f"{move_ext:04X}",
            "destination_register": f"D{destination}",
            "transfer_opcode": f"{transfer_opcode:04X}",
            "transfer_extension": f"{transfer_ext:04X}",
            "table_base": f"{read_base:06X}",
            "candidate_offsets_policy": "Enumerate even offsets 0..254 from the full byte domain; no table length is inferred.",
            "candidate_roots": [], "odd_target_candidates": [],
            "out_of_image_table_words": [], "out_of_image_targets": [],
        }
        for index in range(0, 256, 2):
            cell = read_base + index
            raw = _word_at(image, image_base, cell)
            if raw is None:
                entry["out_of_image_table_words"].append({"index": index, "cell": f"{cell:08X}"})
                continue
            target = branch_base + signed16(raw)
            candidate = {"index": index, "cell": f"{cell:08X}", "word": f"{raw:04X}",
                         "signed_offset": signed16(raw), "target": f"{target:08X}"}
            if target & 1:
                entry["odd_target_candidates"].append(candidate)
            elif target < image_base or target + 1 >= image_end:
                entry["out_of_image_targets"].append(candidate)
            else:
                entry["candidate_roots"].append(candidate)
                targets.add(target)
        qualified.append(entry)
    return qualified, unqualified, targets


def read_coverage(path: Path) -> list[dict[str, str]]:
    try:
        with path.open("r", newline="", encoding="utf-8-sig") as stream:
            reader = csv.DictReader(stream)
            required = {"pc", "opcode", "words", "nominal_cycles", "kind"}
            if reader.fieldnames is None or not required.issubset(reader.fieldnames):
                raise CatalogError(f"coverage CSV is missing required columns: {path}")
            return list(reader)
    except OSError as exc:
        raise CatalogError(f"cannot read generator coverage CSV {path}: {exc}") from exc


def validate_coverage_rows(image: bytes, image_base: int,
                           rows: list[dict[str, str]]) -> list[dict]:
    failures: list[dict] = []
    seen: set[int] = set()
    known_kinds = {"native", "illegal", "covered", "unsupported"}
    image_end = image_base + len(image)
    for line, row in enumerate(rows, start=2):
        try:
            pc = int(row["pc"], 16)
            opcode = int(row["opcode"], 16)
            word_count = int(row["words"], 10)
            cycles = int(row["nominal_cycles"], 10)
            kind = row["kind"]
        except (KeyError, TypeError, ValueError):
            failures.append({"line": line, "error": "malformed coverage row fields", "row": row})
            continue
        errors: list[str] = []
        if pc in seen:
            errors.append("duplicate PC")
        seen.add(pc)
        if pc < image_base or pc & 1 or pc + 1 >= image_end:
            errors.append("PC is odd or outside image")
        if word_count <= 0 or pc + 2 * word_count > image_end:
            errors.append("instruction word count is nonpositive or extends outside image")
        if cycles <= 0 or cycles > 0xFFFF:
            errors.append("nominal cycle count is outside 1..65535")
        if kind not in known_kinds:
            errors.append(f"unknown coverage kind {kind!r}")
        if image_base <= pc and pc + 1 < image_end:
            actual_opcode = _word_at(image, image_base, pc)
            if actual_opcode != opcode:
                errors.append("CSV opcode does not match image bytes")
        if errors:
            failures.append({"line": line, "pc": f"{pc:06X}", "errors": errors})
    return failures


_MODULE_ENTRY = re.compile(r"\{0x([0-9A-Fa-f]+)u,\{([^}]*)\},(\d+),(\d+),")


def audit_module(image: bytes, image_base: int, module_path: Path,
                 rows: list[dict[str, str]], prefix: str) -> dict:
    source = module_path.read_text(encoding="utf-8")
    guards: list[int] = []
    mismatches: list[dict] = []
    row_by_pc = {int(row["pc"], 16): row for row in rows}
    for match in _MODULE_ENTRY.finditer(source):
        pc = int(match.group(1), 16)
        words = [int(token.strip(), 16) for token in match.group(2).split(",") if token.strip()]
        count = int(match.group(3))
        cycles = int(match.group(4))
        offset = pc - image_base
        expected = None
        if offset >= 0 and offset + 2 * count <= len(image):
            expected = [int.from_bytes(image[i:i + 2], "big")
                        for i in range(offset, offset + 2 * count, 2)]
        if expected != words or len(words) != count:
            mismatches.append({"pc": f"{pc:06X}", "emitted_words": words,
                               "expected_words": expected, "entry_count": count})
        csv_row = row_by_pc.get(pc)
        if csv_row is None:
            mismatches.append({"pc": f"{pc:06X}", "error": "module entry has no coverage row"})
        else:
            try:
                row_opcode = int(csv_row["opcode"], 16)
                row_words = int(csv_row["words"], 10)
                row_cycles = int(csv_row["nominal_cycles"], 10)
            except (KeyError, ValueError):
                row_opcode = row_words = row_cycles = -1
            if (not words or row_opcode != words[0] or row_words != count or
                    row_cycles != cycles):
                mismatches.append({"pc": f"{pc:06X}", "error": "module entry disagrees with coverage row",
                                   "module_opcode": None if not words else f"{words[0]:04X}",
                                   "coverage_opcode": csv_row.get("opcode"),
                                   "module_words": count, "coverage_words": csv_row.get("words"),
                                   "module_cycles": cycles, "coverage_cycles": csv_row.get("nominal_cycles")})
        guards.append(pc)
    # Covered callbacks and unsupported operations are intentionally absent
    # from the emitted module; the former must be supplied by the caller.
    metadata_pattern = re.compile(
        r"const\s+M68kStepModule\s+" + re.escape(prefix) +
        r"_module\s*=\s*\{\s*\"" + re.escape(prefix) +
        r"\"\s*,\s*0x([0-9A-Fa-f]+)u\s*,\s*(\d+)u\s*,\s*0x([0-9A-Fa-f]+)u\s*,\s*(entries\s*,\s*sizeof\s+entries\s*/\s*sizeof\s*\*\s*entries|NULL\s*,\s*0)\s*\};")
    metadata = metadata_pattern.search(source)
    expected_module_pcs = {pc for pc, row in row_by_pc.items()
                           if row.get("kind") not in ("covered", "unsupported")}
    if metadata is None:
        mismatches.append({"error": "module descriptor metadata is missing or malformed"})
    else:
        declared_base = int(metadata.group(1), 16)
        declared_size = int(metadata.group(2), 10)
        declared_crc = int(metadata.group(3), 16)
        expected_crc = zlib.crc32(image) & 0xFFFFFFFF
        expected_entries = "entries" if expected_module_pcs else "NULL"
        if (declared_base != image_base or declared_size != len(image) or
                declared_crc != expected_crc or metadata.group(4).lstrip().startswith(expected_entries) is False):
            mismatches.append({"error": "module descriptor metadata disagrees with image or coverage",
                               "module_base": f"0x{declared_base:06X}",
                               "expected_base": f"0x{image_base:06X}",
                               "module_size": declared_size, "expected_size": len(image),
                               "module_crc32": f"{declared_crc:08X}",
                               "expected_crc32": f"{expected_crc:08X}",
                               "module_entries": metadata.group(4).strip(),
                               "expected_entries": expected_entries})
    if len(guards) != len(set(guards)):
        # Preserve the duplicate as a concrete audit failure even if the PC set matches.
        duplicate_pcs = sorted(pc for pc, count in Counter(guards).items() if count > 1)
        mismatches.append({"error": "duplicate module guard row",
                           "pcs": [f"{pc:06X}" for pc in duplicate_pcs]})
    if set(guards) != expected_module_pcs:
        mismatches.append({"error": "module/coverage PC sets differ",
                           "module_only": [f"{v:06X}" for v in sorted(set(guards) - expected_module_pcs)],
                           "coverage_only": [f"{v:06X}" for v in sorted(expected_module_pcs - set(guards))]})
    return {"module_guard_rows": len(guards), "coverage_rows": len(rows),
            "exact_guard_matches": max(0, len(guards) - sum(1 for m in mismatches if "pc" in m)),
            "full_coverage_correspondence": set(guards) == expected_module_pcs and len(guards) == len(set(guards)),
            "failures": mismatches}


def _write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _tree_hashes(directory: Path) -> list[dict[str, object]]:
    files = []
    for path in sorted(directory.rglob("*")):
        if path.is_file():
            files.append({"path": path.relative_to(directory).as_posix(),
                          "bytes": path.stat().st_size, "sha256": sha256_file(path)})
    return files


def run(args: argparse.Namespace) -> int:
    image_path = args.image.resolve(strict=True)
    generator = args.generator.resolve(strict=True)
    output = args.output_dir.resolve()
    if not image_path.is_file() or not generator.is_file():
        raise CatalogError("image and generator must be regular files")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,95}", args.prefix):
        raise CatalogError("prefix must be a C identifier no longer than 96 characters")
    if args.load_base < 0 or args.load_base & 1 or args.load_base > 0xFFFFFF:
        raise CatalogError("load base must be a nonnegative, even 24-bit address")
    if args.max_iterations < 1:
        raise CatalogError("max iterations must be positive")
    image = image_path.read_bytes()
    if not image or len(image) & 1 or args.load_base + len(image) > 0x1000000:
        raise CatalogError("image must be nonempty, word-aligned, and fit in the 24-bit address space")
    roots = parse_roots(args.roots_file, args.entries, args.load_base, len(image))
    covered = [path.resolve(strict=True) for path in args.covered_entries_file]
    if any(not path.is_file() for path in covered):
        raise CatalogError("each covered-entries path must be a regular file")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise CatalogError(f"output directory must be absent or empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    image_hash = sha256_bytes(image)
    gen_hash = sha256_file(generator)
    covered_records = [{"path": str(path), "bytes": path.stat().st_size,
                       "sha256": sha256_file(path)} for path in covered]
    iterations: list[dict] = []
    current_roots = set(roots)
    fixed = False
    final_dir: Path | None = None
    final_rows: list[dict[str, str]] = []
    final_dispatches: list[dict] = []
    final_unqualified: list[dict] = []
    final_targets: set[int] = set()
    failure: str | None = None

    for index in range(args.max_iterations):
        iter_dir = output / f"iteration-{index:03d}"
        catalog_dir = iter_dir / "catalog"
        iter_dir.mkdir()
        catalog_dir.mkdir()
        root_file = iter_dir / "roots.txt"
        root_file.write_text("".join(f"0x{root:06X}\n" for root in sorted(current_roots)), encoding="ascii")
        command = [str(generator), str(image_path), str(catalog_dir), args.prefix,
                   f"0x{args.load_base:X}", "--entries-file", str(root_file)]
        for covered_path in covered:
            command.extend(("--covered-entries-file", str(covered_path)))
        log_path = iter_dir / "generation.log"
        with log_path.open("wb") as log:
            result = subprocess.run(command, cwd=str(iter_dir), stdout=log,
                                    stderr=subprocess.STDOUT, check=False)
        info = {"iteration": index, "input_root_count": len(current_roots),
                "roots_file": str(root_file), "roots_sha256": sha256_file(root_file),
                "catalog_directory": str(catalog_dir), "command": command,
                "generator_exit_code": result.returncode,
                "generation_log": str(log_path), "generation_log_sha256": sha256_file(log_path)}
        if result.returncode not in (0, 3):
            info["failure"] = f"generator exited with {result.returncode}"
            info["catalog_files"] = _tree_hashes(catalog_dir)
            iterations.append(info)
            failure = info["failure"]
            final_dir = iter_dir
            break

        coverage_path = catalog_dir / f"{args.prefix}_coverage.csv"
        module_path = catalog_dir / f"{args.prefix}_module.c"
        if not coverage_path.is_file() or not module_path.is_file():
            info["failure"] = "generator omitted expected coverage/module outputs"
            iterations.append(info)
            failure = info["failure"]
            final_dir = iter_dir
            break
        rows = read_coverage(coverage_path)
        coverage_failures = validate_coverage_rows(image, args.load_base, rows)
        row_pcs = {int(row["pc"], 16) for row in rows
                   if row.get("pc") and re.fullmatch(r"[0-9A-Fa-f]+", row["pc"])}
        missing_roots = sorted(current_roots - row_pcs)
        if coverage_failures or missing_roots:
            info["failure"] = "coverage rows are invalid or one or more requested roots are absent"
            info["coverage_validation_failures"] = coverage_failures
            info["missing_roots"] = [f"0x{x:06X}" for x in missing_roots]
            info["coverage_rows"] = len(rows)
            info["catalog_files"] = _tree_hashes(catalog_dir)
            iterations.append(info)
            failure = info["failure"]
            final_dir, final_rows = iter_dir, rows
            break
        dispatches, unqualified, candidate_targets = analyze_dispatches(image, args.load_base, rows)
        new_roots = candidate_targets - current_roots
        kind_counts: dict[str, int] = {}
        for row in rows:
            kind_counts[row["kind"]] = kind_counts.get(row["kind"], 0) + 1
        if result.returncode == 3 and not kind_counts.get("unsupported", 0):
            info["failure"] = "generator returned its unsupported-operation status but coverage has no unsupported rows"
            info["coverage_rows"] = len(rows)
            info["kind_counts"] = kind_counts
            info["catalog_files"] = _tree_hashes(catalog_dir)
            iterations.append(info)
            failure = info["failure"]
            final_dir, final_rows = iter_dir, rows
            break
        guard_audit = audit_module(image, args.load_base, module_path, rows, args.prefix)
        analysis = {"qualified_dispatches": dispatches, "unqualified_forms": unqualified,
                    "candidate_target_roots": [f"0x{x:06X}" for x in sorted(candidate_targets)],
                    "new_roots": [f"0x{x:06X}" for x in sorted(new_roots)],
                    "generator_kind_counts": kind_counts, "guard_audit": guard_audit}
        _write_json(iter_dir / "dispatch-analysis.json", analysis)
        info.update({"coverage_rows": len(rows), "kind_counts": kind_counts,
                     "qualified_dispatches": len(dispatches),
                     "unqualified_forms": len(unqualified),
                     "candidate_root_count": len(candidate_targets),
                     "new_root_count": len(new_roots),
                     "guard_audit": guard_audit,
                     "catalog_files": _tree_hashes(catalog_dir)})
        iterations.append(info)
        final_dir, final_rows = iter_dir, rows
        final_dispatches, final_unqualified, final_targets = dispatches, unqualified, candidate_targets
        if guard_audit["failures"]:
            failure = "generated module guard audit failed"
            break
        if not new_roots:
            fixed = True
            break
        current_roots.update(new_roots)

    if final_dir is None:
        raise CatalogError("no generation iteration completed")
    unsupported = sum(row["kind"] == "unsupported" for row in final_rows)
    illegal = sum(row["kind"] == "illegal" for row in final_rows)
    native = sum(row["kind"] == "native" for row in final_rows)
    generator_reported_unsupported = any(item["generator_exit_code"] == 3 for item in iterations)
    status = ("failed" if failure else "fixed_point_with_unsupported_rows"
              if fixed and (unsupported or generator_reported_unsupported)
              else "fixed_point" if fixed else "iteration_limit")
    report = {
        "schema": "m68k-byte-index-catalog-v1",
        "status": status,
        "failure": failure,
        "inputs": {
            "image": str(image_path), "image_bytes": len(image), "image_sha256": image_hash,
            "generator": str(generator), "generator_bytes": generator.stat().st_size,
            "generator_sha256": gen_hash, "prefix": args.prefix,
            "load_base": f"0x{args.load_base:06X}",
            "initial_roots": [f"0x{x:06X}" for x in sorted(roots)],
            "initial_roots_sha256": sha256_bytes(
                "".join(f"0x{root:06X}\n" for root in sorted(roots)).encode("ascii")),
            "initial_roots_file": str(args.roots_file.resolve()) if args.roots_file else None,
            "initial_roots_file_sha256": sha256_file(args.roots_file.resolve()) if args.roots_file else None,
            "covered_entries_files": covered_records,
            "max_iterations": args.max_iterations,
        },
        "algorithm": {
            "pattern": "MOVEQ #0,Dindex; MOVE.B disp(A0),Dindex; MOVE.W d8(PC,Dindex.W),Ddest; immediately adjacent indexed JSR/JMP d8(PC,Ddest.W)",
            "brief_extensions": "Dn.W only; A-register, long-index, scale, and reserved-bit forms are not qualified.",
            "pc_displacements": "Signed low byte of each brief extension; extension word is not treated as displacement.",
            "index_domain": "MOVE.B after exact MOVEQ #0 gives unsigned values 0..255; enumerate only even word-table byte offsets 0..254; never infer table length.",
            "outcomes": "Odd targets, out-of-image target words, out-of-image table cells, and even in-image targets are recorded separately.",
            "odd_table_base": "Odd effective table bases are left unqualified because this closure enumerates even offsets only; odd byte indices are not modeled, so no claim is made about their effective addresses.",
            "coverage_dependency": "Covered-entry files are explicit caller-owned build dependencies; omitted callbacks require the caller to retain the corresponding baseline catalog.",
        },
        "iterations": iterations,
        "fixed_point": {"reached": fixed, "iteration": iterations[-1]["iteration"],
                        "roots": len(current_roots), "dispatch_candidate_union": len(final_targets),
                        "qualified_dispatches": len(final_dispatches),
                        "unqualified_forms": len(final_unqualified),
                        "coverage_rows": len(final_rows), "native_rows": native,
                        "architectural_illegal_rows": illegal, "unsupported_rows": unsupported,
                        "generator_reported_unsupported": generator_reported_unsupported,
                        "unsupported_rows_are_preserved": True},
        "root_coverage_audit": {
            "requested_roots": [f"0x{x:06X}" for x in sorted(current_roots)],
            "missing_roots": iterations[-1].get("missing_roots", []),
            "every_requested_root_reported": not iterations[-1].get("missing_roots", []),
        },
        "final_catalog": str(final_dir / "catalog"),
        "final_catalog_files": _tree_hashes(final_dir / "catalog") if (final_dir / "catalog").is_dir() else [],
        "candidate_tables": final_dispatches,
        "unqualified_forms": final_unqualified,
        "limitations": [
            "Candidate target addresses are conservative possible entry roots, not validated state names or proof of runtime reachability.",
            "Unsupported generator rows remain in the generated catalog and are reported; this tool does not filter or provide runtime fallback.",
            "This is static candidate generation only, not a complete build or gameplay qualification.",
        ],
    }
    manifest_path = output / "manifest.json"
    _write_json(manifest_path, report)
    print(f"status: {status}")
    print(f"manifest: {manifest_path}")
    print(f"catalog: {report['final_catalog']}")
    if failure:
        raise CatalogError(failure)
    if not fixed:
        raise CatalogError(f"did not reach fixed point within {args.max_iterations} iterations; see {manifest_path}")
    if unsupported or generator_reported_unsupported:
        raise CatalogError(f"fixed point contains {unsupported} unsupported generator rows; see {manifest_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path, help="one mapped, even-sized owned 68000 image")
    parser.add_argument("--output-dir", required=True, type=Path, help="fresh/empty directory for all generated artifacts")
    parser.add_argument("--generator", required=True, type=Path, help="M68kStepRecomp executable")
    parser.add_argument("--prefix", required=True, help="C identifier prefix for generated catalog")
    parser.add_argument("--load-base", required=True, type=lambda v: int(v, 0))
    parser.add_argument("--entries-file", dest="roots_file", type=Path,
                        help="optional whitespace-separated initial root addresses")
    parser.add_argument("--entry", dest="entries", action="append", default=[],
                        help="initial root address; may be repeated")
    parser.add_argument("--covered-entries-file", type=Path, action="append", default=[],
                        help="explicit already-covered entries file passed to every generator iteration")
    parser.add_argument("--max-iterations", type=int, default=16)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return run(args)
    except (CatalogError, OSError, subprocess.SubprocessError) as exc:
        print(f"m68k_catalog: error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

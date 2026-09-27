#!/usr/bin/env python3
"""Build pinned stock disassemblies in private output trees and verify ROMs.

Never edits a submodule or a supplied ROM. Sonic 1's upstream default is REV01;
the engine uses REV00. That single build option is changed in the exported
build tree. Listings are accepted only after byte-identical ROM validation.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "sonic1": ("sonicthehedgehog/s1disasm", "d343882f75b13646ba50ae06b1486f4bd05cb738", "build.lua", "s1built.bin", "sonic.lst", "sonicthehedgehog/sonic.bin"),
}
FOLDERS = {"sonic1": "sonicthehedgehog"}
# Multi-ROM images (lock-on carts) built by concatenating SOURCES outputs.
# Game repositories supply their own entries, e.g. through a wrapper script:
#   name: {"parts": [(game, offset), ...], "reference": "<repo-relative ROM>",
#          "metadata": {extra provenance keys}}
# Parts must be contiguous; labels of each part are relocated by its offset.
COMPOSITES: dict[str, dict] = {}

# Include-file depth is not a CPU indicator: much of Sonic 1's 68000 code lives
# in (1)/(2) records. Require a known 68000 mnemonic and matching ROM bytes.
ROW = re.compile(r"^(?:\(\d+\))?\s*\d+/\s*([\da-fA-F]+)\s*:\s?(.*)$")
EMIT = re.compile(r"^\s*([\da-fA-F]{2,8}(?: +[\da-fA-F]{2,8})*)\s+(\S+)")
LABEL = re.compile(r"^\s*(?:\(MACRO\)\s*)?([A-Za-z_][\w.]*):")
M68K = re.compile(r"^!?(?:abcd|add[aqix]?|and[i]?|asl|asr|b(?:cc|cs|eq|ge|gt|hi|le|ls|lt|mi|ne|pl|ra|sr|vc|vs)|bchg|bclr|bset|btst|chk|clr|cmp[aim]?|db\w+|div[su]|eor[i]?|exg|ext|illegal|jmp|jsr|lea|link|lsl|lsr|move[amq]?|movem|movep|muls|mulu|nbcd|negx?|nop|not|or[i]?|pea|reset|rol|ror|roxl|roxr|rte|rtr|rts|sbcd|s(?:cc|cs|eq|f|ge|gt|hi|le|ls|lt|mi|ne|pl|t|vc|vs)|stop|sub[aqix]?|swap|tas|trapv?|tst|unlk)(?:\.[bwls])?$", re.I)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def symbols(listing: Path, rom: bytes) -> dict[int, list[str]]:
    labels: dict[int, list[str]] = {}
    code = set()
    for line in listing.read_text(encoding="utf-8", errors="replace").splitlines():
        row = ROW.match(line)
        if not row:
            continue
        address, body = int(row[1], 16), row[2]
        if not 0x200 <= address < len(rom) or address & 1:
            continue
        label = LABEL.match(body)
        if label and not re.match(r"^(?:loc_|locret_|sub_|byte_|word_|off_|j_)", label[1]):
            names = labels.setdefault(address, [])
            if label[1] not in names:
                names.append(label[1])
        emit = EMIT.match(body)
        if emit and M68K.fullmatch(emit[2]):
            data = bytes.fromhex(emit[1])
            if rom[address:address + len(data)] == data:
                code.add(address)
    return {a: names for a, names in sorted(labels.items()) if a in code}


def export(game: str, rom: bytes, names: dict[int, list[str]], out: Path, source: dict) -> None:
    csv = "# Verified stock disassembly; regenerate with tools/sonic_disassembly.py\n"
    csv += "".join(f"{a:06X},{n[0]},{' / '.join(n[1:])}\n" for a, n in names.items())
    (out / f"{game}.annotations.csv").write_text(csv, encoding="utf-8", newline="\n")
    annotation = {
        "format": "ghidra-human-annotations-v1", "program": f"/{game}.bin",
        "languageId": "68000:BE:32:default", "executableMd5": hashlib.md5(rom).hexdigest(),
        "provenance": source,
        "symbols": [{"address": f"{a:08x}", "name": n[0], "namespace": "Global", "symbolType": "Label", "primary": True} for a, n in names.items()],
        "comments": [], "bookmarks": [], "functionSignatures": [], "compositeTypes": [],
    }
    (out / f"{game}.annotations.json").write_text(json.dumps(annotation, indent=2) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=ROOT / "build/disassembly")
    ap.add_argument("--games", nargs="+", choices=list(SOURCES), default=list(SOURCES))
    ap.add_argument("--reuse", action="store_true", help="Recheck existing build outputs against the supplied ROMs")
    ap.add_argument("--install", action="store_true", help="Install verified annotation CSV/JSON and provenance into the repository")
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    results = {}
    for game in args.games:
        relative, revision, script, binary, listing, reference = SOURCES[game]
        source = ROOT / relative
        have = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if have != revision:
            raise RuntimeError(f"{game}: submodule must be at {revision}, found {have}")
        work = out / game
        if work.exists() and not args.reuse:
            raise RuntimeError(f"Refusing to reuse build directory {work}; choose a fresh --out")
        if not args.reuse:
            work.mkdir()
            archive = subprocess.check_output(["git", "-C", str(source), "archive", "HEAD"])
            with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
                tar.extractall(work, filter="data")
            if game == "sonic1":
                path = work / "sonic.asm"
                text, count = re.subn(r"(?m)^Revision = 1$", "Revision = 0", path.read_text(encoding="utf-8"))
                if count != 1:
                    raise RuntimeError("Sonic 1 revision selector changed")
                path.write_text(text, encoding="utf-8", newline="\n")
            lua = work / "build_tools/Lua/lua.exe" if sys.platform == "win32" else Path("lua")
            subprocess.run([str(lua), script], cwd=work, check=True)
        built, rom = (work / binary).read_bytes(), (ROOT / reference).read_bytes()
        if built != rom:
            raise RuntimeError(f"{game}: ROM mismatch: build {sha(built)}, expected {sha(rom)}")
        metadata = {"source": relative, "commit": revision, "rom_sha256": sha(rom), "build": script,
                    "options": {"Revision": 0} if game == "sonic1" else {}}
        names = symbols(work / listing, rom)
        if len(names) < 500:
            raise RuntimeError(f"{game}: unexpectedly few code labels ({len(names)})")
        export(game, rom, names, out, metadata)
        results[game] = {**metadata, "code_labels": len(names)}
        print(f"{game}: byte-identical, {len(names)} code labels", flush=True)
    for composite, spec in COMPOSITES.items():
        parts = spec["parts"]
        if not all(game in results for game, _ in parts):
            continue
        built, names = b"", {}
        for game, offset in parts:
            relative, revision, script, binary, listing, reference = SOURCES[game]
            if offset != len(built):
                raise RuntimeError(f"{composite}: {game} must start at {len(built):#x}, not {offset:#x}")
            part = (out / game / binary).read_bytes()
            names.update({a + offset: n for a, n in symbols(out / game / listing, part).items()})
            built += part
        rom = (ROOT / spec["reference"]).read_bytes()
        if built != rom:
            raise RuntimeError(f"{composite} does not match the concatenated stock ROMs")
        first = SOURCES[parts[0][0]]
        metadata = {"source": first[0], "commit": first[1], "rom_sha256": sha(rom), **spec["metadata"]}
        export(composite, rom, names, out, metadata)
        results[composite] = {**metadata, "code_labels": len(names)}
        print(f"{composite}: byte-identical lock-on, {len(names)} code labels", flush=True)
    (out / "provenance.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8", newline="\n")
    if args.install:
        import shutil
        target = ROOT / "ghidra/annotations"
        target.mkdir(parents=True, exist_ok=True)
        for game in results:
            shutil.copyfile(out / f"{game}.annotations.csv", ROOT / FOLDERS[game] / "annotations_from_disasm.csv")
            shutil.copyfile(out / f"{game}.annotations.json", target / f"{game}.annotations.json")
        shutil.copyfile(out / "provenance.json", target / "provenance.json")


if __name__ == "__main__":
    main()

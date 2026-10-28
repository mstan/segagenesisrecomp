#!/usr/bin/env python3
"""Compact ordered generated M68kStep catalogs without changing the step ABI.

Input lines are GROUP|directory|prefix in lookup-priority order. Operations must
use one common m68k_step_bind.h and compilation profile. Only the ordinary
generator's standalone function/descriptor format is accepted. Equal operation
bodies may share an existing exported symbol; no guest code is decoded here.
The generated metadata links against the ORIGINAL operation objects.

All opcode/extension guards, cycles and distinct per-PC variants remain. Fetch
is side-effect-free by the M68kStepBus contract. No code-identity caching, ROM
knowledge, HLE, fast math, assembly or runtime format is introduced.
"""
from __future__ import annotations

import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path
import re

IDENT = r"[A-Za-z_][A-Za-z_0-9]*"
FUNCTION = re.compile(r"^void (" + IDENT + r")\(M68kStepContext \*ctx\)\{\n(.*?)^\}\n", re.M | re.S)
COMMENTS = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
UNSAFE = re.compile(r'''["'#]|\b(?:static|extern|asm|__asm__|__func__|__FUNCTION__|__FILE__|__LINE__|__COUNTER__|__DATE__|__TIME__)\b|__builtin_''')
ENTRY = re.compile(r" \{0x([0-9A-Fa-f]+)u,\{([^}]*)\},(\d+),(\d+),(" + IDENT + r")\},")
BUILTINS = {"m68k_step_illegal", "m68k_step_unimplemented"}


class CatalogError(ValueError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_source(path: Path, sources: dict) -> str:
    raw = path.read_bytes()
    sources[str(path.resolve())] = digest(raw)
    return raw.decode("utf-8").replace("\r\n", "\n")


def operations(text: str, prefix: str):
    remainder = COMMENTS.sub("", FUNCTION.sub("", text)).strip()
    if remainder != '#include "m68k_step_bind.h"':
        raise CatalogError("operation translation unit has an unsupported prelude or body")
    for match in FUNCTION.finditer(text):
        symbol, body = match.groups()
        if not re.fullmatch(re.escape(prefix) + r"_at_[0-9A-Fa-f]{6}", symbol):
            raise CatalogError(f"unexpected generated operation: {symbol}")
        # Exact text, not algebra or alpha equivalence. Source-location, local
        # static and preprocessing effects must retain their original identity.
        yield symbol, None if UNSAFE.search(body) else digest(body.encode("utf-8"))


def entries(text: str, prefix: str, aliases: dict):
    clean = COMMENTS.sub("", text).strip()
    start = '#include "' + prefix + '.h"'
    if not clean.startswith(start):
        raise CatalogError("unsupported generated module include")
    clean = clean[len(start):].lstrip()
    declaration = re.compile(r"extern void (" + IDENT + r")\(M68kStepContext \*\);\s*")
    position = 0
    while match := declaration.match(clean, position):
        position = match.end()
    begin = "static const M68kStepEntry entries[]={\n"
    if not clean.startswith(begin, position):
        raise CatalogError("unsupported generated entry array")
    lines = clean[position + len(begin):].splitlines()
    previous = -1
    result = []
    for index, line in enumerate(lines):
        if line == "};":
            tail = "\n".join(lines[index + 1:])
            pattern = (r'const M68kStepModule ' + re.escape(prefix) + r'_module=\{"' + re.escape(prefix) +
                       r'",(0x[0-9A-Fa-f]+|[0-9]+)u,(0x[0-9A-Fa-f]+|[0-9]+)u,'
                       r'0x[0-9A-Fa-f]+u,entries,sizeof entries/sizeof \*entries\};')
            identity = re.fullmatch(pattern, tail)
            if not identity:
                raise CatalogError("unsupported generated module identity/tail")
            base, size = (int(v, 0) for v in identity.groups())
            if not size or base + size > 0x1000000:
                raise CatalogError("invalid source module bounds")
            if any(pc < base or pc + len(words) * 2 > base + size for pc, words, _, _ in result):
                raise CatalogError("source descriptor outside module bounds")
            return result
        match = ENTRY.fullmatch(line)
        if not match:
            raise CatalogError(f"unsupported generated descriptor: {line[:120]}")
        pc = int(match[1], 16)
        words = tuple(int(v.strip(), 16) for v in match[2].split(","))
        count, cycles, symbol = int(match[3]), int(match[4]), match[5]
        if pc <= previous or pc & 1 or pc > 0xFFFFFF or len(words) != count or not 1 <= count <= 11:
            raise CatalogError("invalid descriptor PC/order/guard length")
        if not 1 <= cycles <= 65535 or any(not 0 <= word <= 65535 for word in words):
            raise CatalogError("invalid descriptor cycles/word")
        if symbol not in aliases and symbol not in BUILTINS:
            raise CatalogError(f"operation missing from qualified translation units: {symbol}")
        previous = pc
        result.append((pc, words, cycles, aliases.get(symbol, symbol)))
    raise CatalogError("unterminated generated entry array")


def compact(catalog_list: Path, output: Path, *, parts: int = 32, chunk_entries: int = 16384) -> dict:
    if parts < 1 or chunk_entries < 1:
        raise CatalogError("parts and chunk entries must be positive")
    output.mkdir(parents=True, exist_ok=True)
    groups = collections.OrderedDict()
    sources, canonical, symbols, module_rows = {}, {}, set(), []
    input_raw = catalog_list.read_bytes()
    catalogs = []
    for line in input_raw.decode("utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        group, directory, prefix = line.split("|")
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", group) or not re.fullmatch(IDENT, prefix):
            raise CatalogError("invalid group or prefix")
        catalogs.append((group, Path(directory).resolve(strict=True), prefix))
        groups.setdefault(group, {"seen": set(), "variants": {}, "input_entries": 0})
    if not catalogs:
        raise CatalogError("empty catalog list")
    alias_path = output / "native_catalog_aliases.jsonl.gz"
    with alias_path.open("wb") as raw_aliases, gzip.GzipFile(fileobj=raw_aliases, mode="wb", mtime=0) as audit:
        for group, directory, prefix in catalogs:
            aliases = {}
            for part in range(parts):
                path = directory / f"{prefix}_part{part:02d}.c"
                for symbol, body_hash in operations(read_source(path, sources), prefix):
                    if symbol in symbols:
                        raise CatalogError(f"duplicate exported operation: {symbol}")
                    symbols.add(symbol)
                    target = canonical.setdefault(body_hash, symbol) if body_hash else symbol
                    aliases[symbol] = target
                    audit.write((json.dumps([symbol, target, body_hash], separators=(",", ":")) + "\n").encode())
            module = entries(read_source(directory / f"{prefix}_module.c", sources), prefix, aliases)
            state = groups[group]
            state["input_entries"] += len(module)
            for record in module:
                if record not in state["seen"]:
                    state["seen"].add(record)
                    state["variants"].setdefault(record[0], []).append(record)
            module_rows.append({"group": group, "prefix": prefix, "entries": len(module)})
            if len(module_rows) % 16 == 0:
                print(f"Qualified {len(module_rows)} generated catalogs", flush=True)
    generated, declarations, macros, stats = [], [], [], {}
    for group, state in groups.items():
        if not state["seen"]:
            raise CatalogError(f"group {group} has no descriptors")
        # Each shard has at most one entry per PC. Variant-major traversal keeps
        # the ORIGINAL priority at every PC even when shards cover many images.
        ordered = sorted(state["variants"].items())
        modules = []
        for variant in range(max((len(v) for _, v in ordered), default=0)):
            layer = [v[variant] for _, v in ordered if variant < len(v)]
            for begin in range(0, len(layer), chunk_entries):
                records = layer[begin:begin + chunk_entries]
                name = f"genesis_catalog_{group.lower()}_{len(modules):04d}"
                path = output / f"{name}.c"
                with path.open("w", encoding="utf-8", newline="\n") as stream:
                    stream.write('/* Generated guarded metadata; operation bodies remain in their qualified objects. */\n#include "m68k_step.h"\n')
                    for target in sorted({r[3] for r in records} - BUILTINS):
                        stream.write(f"extern void {target}(M68kStepContext *);\n")
                    stream.write("static const M68kStepEntry entries[]={\n")
                    for pc, words, cycles, target in records:
                        values = ",".join(f"0x{w:04X}" for w in words)
                        stream.write(f" {{0x{pc:06X}u,{{{values}}},{len(words)},{cycles},{target}}},\n")
                    stream.write('};\nconst M68kStepModule ' + name + '_module={"' + name + '",0u,0x1000000u,0u,entries,sizeof entries/sizeof *entries};\n')
                generated.append(path)
                modules.append(name + "_module")
                declarations.append("extern const M68kStepModule " + name + "_module;")
        macros.append("#define GENESIS_CATALOG_" + group + "_MODULES " + ",".join("&" + n for n in modules))
        stats[group] = {"input_entries": state["input_entries"], "unique_entries": len(state["seen"]), "output_modules": len(modules)}
    header = output / "native_catalog_registry.h"
    header.write_text('#ifndef GENESIS_NATIVE_CATALOG_REGISTRY_H\n#define GENESIS_NATIVE_CATALOG_REGISTRY_H\n#include "m68k_step.h"\n' + "\n".join(declarations + macros) + "\n#endif\n", encoding="utf-8", newline="\n")
    generated.append(header)
    manifest = {"schema": "M68kStepCompactCatalog1", "generator_sha256": digest(Path(__file__).read_bytes()),
                "catalog_list_sha256": digest(input_raw), "groups": stats, "modules": module_rows,
                "input_sources": sources, "outputs": {p.name: digest(p.read_bytes()) for p in generated},
                "alias_map_sha256": digest(alias_path.read_bytes()), "operation_equivalence": "exact body text; unsafe bodies retain symbol identity"}
    cmake = output / "native_catalog_sources.cmake"
    cmake.write_text('set(GENESIS_COMPACT_CATALOG_SOURCES\n' + "".join('  "${CMAKE_CURRENT_LIST_DIR}/' + p.name + '"\n' for p in generated if p.suffix == ".c") + ')\n', encoding="utf-8", newline="\n")
    manifest["outputs"][cmake.name] = digest(cmake.read_bytes())
    (output / "native_catalog_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog-list", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = compact(args.catalog_list, args.output)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"Catalog compaction refused: {error}\n")
    print(json.dumps(result["groups"], indent=2))


if __name__ == "__main__":
    main()

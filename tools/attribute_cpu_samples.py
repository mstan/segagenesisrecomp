#!/usr/bin/env python3
"""Attribute WPA CPU-sampling CSV addresses to MinGW executable symbols.

WPA identifies MinGW PE modules but does not resolve their embedded GNU symbol
tables. This tool filters one process/module from a WPA CPU Usage (Sampled)
export, reverses the module's ASLR relocation, maps samples to the nearest
preceding defined ``nm`` symbol, and aggregates leaf counts.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path


PE_ALLOCATION_GRANULARITY = 0x10000


def parse_int(value: str) -> int:
    return int(value, 0)


def load_samples(csv_path: Path, process: str, module: str) -> Counter[int]:
    samples: Counter[int] = Counter()
    with csv_path.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            if row.get("Process Name") != process or row.get("Module") != module:
                continue
            address = int(row["Address"], 16)
            samples[address] += int(row["Count"])
    if not samples:
        raise RuntimeError(
            f"no samples found for process={process!r}, module={module!r}"
        )
    return samples


def load_symbols(nm: Path, exe: Path) -> tuple[list[int], list[str]]:
    result = subprocess.run(
        [str(nm), "-n", "-C", "--defined-only", str(exe)],
        check=True,
        capture_output=True,
        text=True,
        errors="replace",
    )
    symbols: dict[int, tuple[str, bool]] = {}
    for line in result.stdout.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) != 3:
            continue
        address_text, symbol_type, name = parts
        if symbol_type.lower() not in {"t", "w"}:
            continue
        try:
            address = int(address_text, 16)
        except ValueError:
            continue
        # Prefer a non-local/public spelling when aliases share an address.
        is_global = symbol_type.isupper()
        old = symbols.get(address)
        if (
            old is None
            or (old[0] == ".text" and name != ".text")
            or (is_global and not old[1])
        ):
            symbols[address] = (name, is_global)
    if not symbols:
        raise RuntimeError(f"no defined text symbols found in {exe}")
    addresses = sorted(symbols)
    names = [symbols[address][0] for address in addresses]
    return addresses, names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, required=True, help="WPA CSV export")
    parser.add_argument("--exe", type=Path, required=True, help="sampled MinGW PE")
    parser.add_argument(
        "--process",
        help="WPA process name (default: executable filename)",
    )
    parser.add_argument(
        "--module",
        help="WPA module name (default: executable filename)",
    )
    parser.add_argument(
        "--nm",
        type=Path,
        help="GNU nm executable (default: first nm on PATH)",
    )
    parser.add_argument(
        "--runtime-base",
        type=parse_int,
        help="ASLR runtime image base (default: inferred from lowest sample)",
    )
    parser.add_argument(
        "--preferred-base",
        type=parse_int,
        help="PE preferred image base (default: inferred from lowest symbol)",
    )
    parser.add_argument("--top", type=int, default=40)
    parser.add_argument("--output-csv", type=Path)
    args = parser.parse_args()

    csv_path = args.csv.resolve(strict=True)
    exe = args.exe.resolve(strict=True)
    process = args.process or exe.name
    module = args.module or exe.name
    if args.nm:
        nm = args.nm.resolve(strict=True)
    else:
        found = shutil.which("nm")
        if not found:
            parser.error("nm not found; pass --nm")
        nm = Path(found)

    samples = load_samples(csv_path, process, module)
    symbol_addresses, symbol_names = load_symbols(nm, exe)
    runtime_base = args.runtime_base
    if runtime_base is None:
        runtime_base = min(samples) & ~(PE_ALLOCATION_GRANULARITY - 1)
    preferred_base = args.preferred_base
    if preferred_base is None:
        preferred_base = symbol_addresses[0] & ~(PE_ALLOCATION_GRANULARITY - 1)

    attributed: Counter[str] = Counter()
    unattributed = 0
    for runtime_address, count in samples.items():
        preferred_address = runtime_address - runtime_base + preferred_base
        index = bisect.bisect_right(symbol_addresses, preferred_address) - 1
        if index < 0:
            unattributed += count
            continue
        attributed[symbol_names[index]] += count

    total = sum(samples.values())
    rows = [
        (name, count, count * 100.0 / total)
        for name, count in attributed.most_common()
    ]
    if args.output_csv:
        output = args.output_csv.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["Function", "Count", "Percent"])
            writer.writerows(
                (name, count, f"{percent:.6f}")
                for name, count, percent in rows
            )

    print(
        f"process={process} module={module} samples={total} "
        f"runtime_base=0x{runtime_base:X} preferred_base=0x{preferred_base:X} "
        f"symbols={len(symbol_addresses)} unattributed={unattributed}"
    )
    print(f"{'Count':>10} {'Percent':>9}  Function")
    for name, count, percent in rows[: args.top]:
        print(f"{count:10d} {percent:8.3f}%  {name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"[attribute_cpu_samples] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)

#!/usr/bin/env python3
"""Generate the complete immutable Genesis DAC color domain."""
import argparse
from pathlib import Path


def table_text():
    ladders = [
        [0, 49, 87, 119, 146, 174, 206, 255],
        [0, 27, 49, 71, 87, 103, 119, 130],
        [130, 146, 157, 174, 190, 206, 228, 255],
    ]
    lines = [
        "/* Generated from the documented DAC ladders in genesis_dac.h.",
        " * Complete 9-bit RGB input domain; no machine state or invalidation. */",
        "#ifndef GENESIS_DAC_TABLE_H",
        "#define GENESIS_DAC_TABLE_H",
        "#include <stdint.h>",
        "static const uint32_t genesis_dac_colors[3][512] = {",
    ]
    for ladder in ladders:
        colors = [0xff000000 | (ladder[k & 7] << 16) |
                  (ladder[(k >> 3) & 7] << 8) | ladder[(k >> 6) & 7]
                  for k in range(512)]
        lines.append("    {")
        for start in range(0, 512, 8):
            lines.append("        " + ", ".join(
                f"0x{color:08x}u" for color in colors[start:start + 8]) + ",")
        lines.append("    },")
    return "\n".join(lines + ["};", "#endif", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] /
                        "runner/video/genesis_dac_table.h")
    args = parser.parse_args()
    data = table_text().encode("utf-8")
    if args.check:
        if args.output.read_bytes() != data:
            raise SystemExit("DAC table differs; regenerate it and run graphics_derivation")
    else:
        args.output.write_bytes(data)


if __name__ == "__main__":
    main()

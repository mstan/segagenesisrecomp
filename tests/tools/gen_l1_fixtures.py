#!/usr/bin/env python3
"""
gen_l1_fixtures.py — Generate L1 decoder-test fixtures from the s1disasm
AS listing file.

Reads:   _s1disasm/sonic.lst   (produced by build.lua at REV00)
Writes:  segagenesisrecomp/tests/fixtures/sonic1/l1/instructions.txt

Each output row describes one 68K instruction the disasm confidently emits:

    <addr_hex> <bytes_hex>\t<mnem>\t<operand_text>

The AS listing already gives us exact (address, assembled bytes, source text)
per line, so the parser is a thin regex — no macro expansion, no conditional
assembly handling. Macro-expanded lines have a (N) prefix; they are parsed
the same way.

Canonicalization is light on purpose: the L1 harness will compare mnemonic
class (not full text). Fuller canonicalization can be layered on when we
add a 68K pretty-printer to the decoder.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[5]
LST_PATH  = REPO_ROOT / "_s1disasm" / "sonic.lst"
OUT_PATH  = Path(__file__).resolve().parents[1] / "fixtures" / "sonic1" / "l1" / "instructions.txt"
# Side-output: every code address the disasm emits, including macro-
# expanded instructions whose bytes-on-listing differ from the final
# ROM (those don't go in the L1 fixture but DO count as code for
# data-as-function detection).
CODE_ADDR_PATH = Path(__file__).resolve().parents[1] / "fixtures" / "sonic1" / "l1" / "code_addresses.txt"

# Primary instruction line:
#   "     212/     236 : 7000                \t\tmoveq\t#0,d0\t; clear d0"
#   "(1)   11/    1396 : 11C0 F00A           \t\tmove.b\td0,(...)"
# Capture: address(hex), byte-words (hex groups), source-text-tail.
LST_LINE_RE = re.compile(
    r"^(?:\(\d+\))?\s*\d+/\s+([0-9A-Fa-f]+)\s*:\s+"     # addr
    r"((?:[0-9A-Fa-f]{4}\s+){1,6})"                      # byte-words (full 16-bit words only)
    r"(.+)$"                                             # tail (source)
)

# Continuation line (AS emits when an instruction assembles to >3 words):
#   "              232 : 2F00             "
# No source-line prefix, just leading whitespace, addr, ':', hex words.
LST_CONT_RE = re.compile(
    r"^\s+([0-9A-Fa-f]+)\s+:\s+((?:[0-9A-Fa-f]{4}\s+)+)\s*$"
)

# Known 68K mnemonics (lowercase, no size suffix). Any first-token not in
# this set is treated as a non-instruction line (directive, macro call,
# or unexpected). Add to this set as gaps show up.
M68K_MNEMS = {
    "abcd","add","adda","addi","addq","addx","and","andi","asl","asr",
    "bcc","bchg","bclr","bcs","beq","bge","bgt","bhi","bhs","ble","blo",
    "bls","blt","bmi","bne","bpl","bra","bset","bsr","btst","bvc","bvs",
    "chk","clr","cmp","cmpa","cmpi","cmpm",
    "dbcc","dbcs","dbeq","dbf","dbge","dbgt","dbhi","dble","dbls","dblt",
    "dbmi","dbne","dbpl","dbra","dbt","dbvc","dbvs",
    "divs","divu","eor","eori","exg","ext",
    "illegal","jmp","jsr","lea","link","lsl","lsr",
    "move","movea","movem","movep","moveq",
    "muls","mulu","nbcd","neg","negx","nop","not","or","ori",
    "pea","reset","rol","ror","roxl","roxr","rte","rtr","rts",
    "sbcd","scc","scs","seq","sf","sge","sgt","shi","sle","sls","slt",
    "smi","sne","spl","st","stop","sub","suba","subi","subq","subx",
    "swap","tas","trap","trapv","tst","unlk",
}

SIZE_SUFFIXES = (".b", ".w", ".l", ".s")

_INLINE_LABEL_RE = re.compile(r"^[A-Za-z_.][\w.]*:\s+")

def first_token_and_size(src: str) -> tuple[str, str]:
    """Return (base_mnem, size_suffix) from the source line.
    `move.l foo,bar` -> ("move", ".l"). `rts` -> ("rts", "").
    `.loop: move.w a2,(a1)+` -> ("move", ".w") — inline labels
    (LABEL: followed by an instruction on the same line, common in
    s1disasm) are stripped before mnemonic detection.
    Returns ("", "") if the first token doesn't look like an
    instruction."""
    s = src.lstrip()
    # Strip trailing comment so a comment-only line is empty.
    s = re.sub(r";.*$", "", s).rstrip()
    if not s:
        return "", ""
    # Strip inline label (LABEL: at the start, followed by whitespace).
    while True:
        m = _INLINE_LABEL_RE.match(s)
        if not m:
            break
        s = s[m.end():]
    # First whitespace-delimited token.
    tok = re.split(r"[\s\t]", s, maxsplit=1)[0].lower()
    if not tok:
        return "", ""
    # AS marks macro-expanded instructions with a leading '!'. Strip the
    # prefix so we recognize the mnemonic, but tag the result with a
    # `!`-prefix on the BASE mnem (after size-suffix removal below) so
    # the caller knows to record-but-not-emit.
    is_macro = False
    if tok.startswith("!"):
        is_macro = True
        tok = tok[1:]
    # Strip size suffix.
    base, sfx_out = tok, ""
    for sfx in SIZE_SUFFIXES:
        if tok.endswith(sfx):
            base, sfx_out = tok[:-len(sfx)], sfx
            break
    if is_macro:
        base = "!" + base
    return base, sfx_out

def normalize_operands(src: str) -> str:
    """Strip leading inline-label + mnemonic, trailing comment, excess
    whitespace. The harness only bucket-compares mnemonics today; operand
    text is kept for drill-down and future full-text canonicalization."""
    s = src
    # Strip comment.
    s = re.sub(r";.*$", "", s).rstrip()
    # Strip inline label(s).
    while True:
        m = _INLINE_LABEL_RE.match(s.lstrip())
        if not m:
            break
        s = s.lstrip()[m.end():]
    # Drop the first token (mnemonic) and any leading whitespace.
    m = re.match(r"^\s*\S+\s*(.*)$", s)
    if not m:
        return ""
    ops = m.group(1).strip()
    # Collapse runs of whitespace (tabs-as-argument-separator in asm).
    ops = re.sub(r"\s+", " ", ops)
    return ops

def bytes_field_to_hex(bytes_field: str) -> str:
    """'11C0 F00A   ' -> '11c0f00a'. AS emits 4-char hex groups per word."""
    return "".join(bytes_field.split()).lower()

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", action="count", default=0, help="verbose")
    ap.add_argument("--lst", type=Path, default=LST_PATH)
    ap.add_argument("-o", "--output", type=Path, default=OUT_PATH,
                    help="instructions.txt output path")
    ap.add_argument("--code-addrs", type=Path, default=None,
                    help="code_addresses.txt output path "
                         "(default: sibling of --output)")
    args = ap.parse_args()

    code_addr_path = args.code_addrs
    if code_addr_path is None:
        # If output overrides the default, sibling-of-output; else legacy default.
        if args.output == OUT_PATH:
            code_addr_path = CODE_ADDR_PATH
        else:
            code_addr_path = args.output.parent / "code_addresses.txt"

    if not args.lst.exists():
        print(f"lst not found: {args.lst}", file=sys.stderr)
        return 2

    rows: list[tuple[int, str, str, str]] = []  # (addr, bytes_hex, mnem, ops)
    code_addrs: set[int] = set()  # all code addresses, including !-macros
    skipped_directive = 0
    skipped_equate = 0
    skipped_unknown = 0
    seen_addrs: set[int] = set()

    with args.lst.open("r", encoding="utf-8", errors="replace") as f:
        lines = [raw.rstrip("\r\n") for raw in f]

    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        m = LST_LINE_RE.match(line)
        if not m:
            continue
        addr_hex, bytes_field, tail = m.group(1), m.group(2), m.group(3)

        # Greedily absorb continuation lines that extend this instruction's
        # byte run (AS splits instructions >3 words across lines).
        try:
            cur_addr = int(addr_hex, 16)
        except ValueError:
            continue
        cur_len = len(bytes_field_to_hex(bytes_field)) // 2
        while i < len(lines):
            cm = LST_CONT_RE.match(lines[i])
            if not cm:
                break
            try:
                cont_addr = int(cm.group(1), 16)
            except ValueError:
                break
            if cont_addr != cur_addr + cur_len:
                break
            bytes_field = bytes_field + cm.group(2)
            cur_len = len(bytes_field_to_hex(bytes_field)) // 2
            i += 1

        # Equate line: "= $..." follows the colon instead of hex words.
        if bytes_field.lstrip().startswith("="):
            skipped_equate += 1
            continue

        mnem, size = first_token_and_size(tail)
        if not mnem:
            continue
        # Macro-expanded instruction marker — track the address as code
        # in the side file, but don't emit it into the L1 fixture.
        is_macro = mnem.startswith("!")
        if is_macro:
            mnem = mnem[1:]
        if mnem not in M68K_MNEMS:
            # Directive (.b-style data, macros, dc.*, align, etc.).
            skipped_directive += 1
            continue

        try:
            addr = int(addr_hex, 16)
        except ValueError:
            continue
        # 68K instructions are word-aligned. Odd addresses come from Z80
        # sound-driver code that AS places into the ROM via a separate
        # CPU context (with its own org) — skip.
        if addr & 1:
            skipped_unknown += 1
            continue

        bh = bytes_field_to_hex(bytes_field)
        # 68K instructions are word-aligned, 1..5 words (2..10 bytes) in
        # the common ISA. Worst-case with both operands using long absolute
        # addressing can reach 12 bytes. Guard against wildly longer runs.
        if len(bh) % 2 or len(bh) < 2 or len(bh) > 24:
            skipped_unknown += 1
            continue

        # Always record the address as code (used by L2 data-as-function
         # check) — including macro-expanded ones whose final ROM bytes
         # don't match the listing.
        code_addrs.add(addr)

        # Macro-expanded instructions don't go in the L1 fixture — their
        # listed bytes are pre-patch and would mismatch our decoder
        # comparing against actual ROM.
        if is_macro:
            continue

        # De-dup: first-seen per address wins. Macro-expanded copies of
        # the same physical bytes (via jsr/jmp through macros) can end up
        # re-listing the same address — keep the first.
        if addr in seen_addrs:
            continue
        seen_addrs.add(addr)

        ops = normalize_operands(tail)
        mnem_out = mnem + size if size else mnem
        rows.append((addr, bh, mnem_out, ops))

    rows.sort(key=lambda r: r[0])

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as f:
        for addr, bh, mnem, ops in rows:
            f.write(f"{addr:08x} {bh}\t{mnem}\t{ops}\n")

    # Side file: every code address (including macro-expanded ones).
    code_addr_path.parent.mkdir(parents=True, exist_ok=True)
    with code_addr_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# Every disasm-emitted code address — used by the L2\n")
        f.write("# data-as-function check. Includes macro-expanded\n")
        f.write("# instructions excluded from the L1 fixture proper.\n")
        for a in sorted(code_addrs):
            f.write(f"{a:08x}\n")

    print(
        f"l1-gen: {len(rows)} instructions, "
        f"{skipped_directive} directives skipped, "
        f"{skipped_equate} equates skipped, "
        f"{skipped_unknown} malformed skipped "
        f"-> {args.output}"
    )
    return 0

if __name__ == "__main__":
    sys.exit(main())

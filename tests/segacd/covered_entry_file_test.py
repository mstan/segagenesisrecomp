#!/usr/bin/env python3
"""Black-box safety tests for exact precompiled-callback omission records."""
import csv
import pathlib
import subprocess
import sys
import tempfile


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def write_words(path, words):
    path.write_bytes(b"".join(int(word).to_bytes(2, "big") for word in words))


def run_case(exe, root, name, words, roots, covered_files=(), extra=()):
    case = root / name
    case.mkdir()
    image = case / "input.bin"
    out = case / "out"
    out.mkdir()
    write_words(image, words)
    cmd = [str(exe), str(image), str(out), f"covered_{name}", "0x1000"]
    cmd += [str(x) for x in roots]
    for covered in covered_files:
        cmd += ["--covered-entries-file", str(covered)]
    cmd += list(extra)
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result, out, case


def no_outputs(out):
    return not any(out.iterdir())


def must_reject(exe, root, name, words, roots, covered_text, extra=()):
    case = root / name
    case.mkdir()
    covered = case / "covered.txt"
    covered.write_text(covered_text, encoding="ascii")
    result, out, _ = run_case(exe, root, name + "_run", words, roots,
                               (covered,), extra)
    check(result.returncode != 0, f"{name}: malformed/stale record accepted")
    check(no_outputs(out), f"{name}: outputs were written before validation failed")
    return result


def main():
    check(len(sys.argv) == 2, "usage: covered_entry_file_test.py M68K_STEP_RECOMP")
    exe = pathlib.Path(sys.argv[1]).resolve()
    check(exe.is_file(), f"generator missing: {exe}")
    with tempfile.TemporaryDirectory(prefix="covered-entry-test-") as temp:
        root = pathlib.Path(temp)

        # A covered first instruction must still contribute its fallthrough
        # and reachable branch targets to the delta CFG.
        image_words = [0x7003, 0x6108, 0x5340, 0x66FA,
                       0x4E72, 0x2300, 0x5241, 0x4E75,
                       0x60FF, 0x4E7A, 0x4E71, 0x4E71,
                       0x4E80, 0x4E71, 0x4E71, 0x203C]
        cover_a = root / "cover-a.txt"
        cover_b = root / "cover-b.txt"
        cover_a.write_text("0x1000 1 4 0x7003\n", encoding="ascii")
        cover_b.write_text("0x1000 1 4 0x7003\n", encoding="ascii")
        good, out, _ = run_case(exe, root, "exact_duplicate", image_words,
                                ("0x1000",), (cover_a, cover_b))
        check(good.returncode == 0, f"identical duplicate/repeated files rejected: {good.stderr}")
        rows = list(csv.DictReader((out / "covered_exact_duplicate_coverage.csv").open(newline="", encoding="utf-8-sig")))
        by_pc = {row["pc"]: row for row in rows}
        check(by_pc.get("001000", {}).get("kind") == "covered", "covered root not reported as covered")
        check(by_pc.get("001002", {}).get("kind") == "native", "CFG did not continue through covered root")
        check(by_pc.get("00100C", {}).get("kind") == "native", "CFG did not follow the covered root's branch")
        module = (out / "covered_exact_duplicate_module.c").read_text(encoding="utf-8")
        check("{0x001000u,{" not in module, "covered root still appears in native entry table")
        check("{0x001002u,{" in module and "{0x00100Cu,{" in module,
              "delta module omitted reachable native callbacks")

        # One-instruction --all case: a valid covered-only module has no entry
        # array but still has image metadata and a usable NULL/count-zero ABI.
        only = root / "covered-only"
        only.mkdir()
        only_image = only / "input.bin"
        write_words(only_image, [0x4E71])
        only_cover = only / "covered.txt"
        only_cover.write_text("0x1000 1 4 0x4E71\n", encoding="ascii")
        only_out = only / "out"
        only_out.mkdir()
        only_result = subprocess.run(
            [str(exe), str(only_image), str(only_out), "covered_only", "0x1000",
             "--all", "--covered-entries-file", str(only_cover)],
            capture_output=True, text=True)
        check(only_result.returncode == 0, f"all-covered image rejected: {only_result.stderr}")
        only_module = (only_out / "covered_only_module.c").read_text(encoding="utf-8")
        check("NULL,0" in only_module, "all-covered module lacks NULL/count-zero descriptor")
        check("static const M68kStepEntry entries[]" not in only_module,
              "all-covered module emitted an empty entry array")

        # Raw-image mismatch, malformed structure, address alignment, and
        # decode-derived size/cycle mismatches must all fail before output.
        must_reject(exe, root, "stale_extension", [0x303C, 0x1234, 0x4E75],
                    ("0x1000",), "0x1000 2 8 0x303C 0x5678\n")
        must_reject(exe, root, "wrong_cycles", [0x7003, 0x4E75],
                    ("0x1000",), "0x1000 1 6 0x7003\n")
        must_reject(exe, root, "wrong_length", [0x303C, 0x1234, 0x4E75],
                    ("0x1000",), "0x1000 1 8 0x303C\n")
        must_reject(exe, root, "truncated_record", [0x7003, 0x4E75],
                    ("0x1000",), "0x1000 1 4\n")
        must_reject(exe, root, "odd_pc", [0x7003, 0x4E75],
                    ("0x1000",), "0x1001 1 4 0x7003\n")
        must_reject(exe, root, "conflicting_duplicate", [0x7003, 0x4E75],
                    ("0x1000",), "0x1000 1 4 0x7003\n0x1000 1 6 0x7003\n")

        # The root terminates before 0x1002. The exact image word is present,
        # but the stale cycle count must still be decoded and rejected before
        # output; validation cannot depend on CFG reachability.
        stale = must_reject(exe, root, "unreachable_wrong_cycles", [0x4E75, 0x4E71],
                            ("0x1000",), "0x1002 1 5 0x4E71\n")
        check("cycles" in stale.stderr.lower() or "length" in stale.stderr.lower(),
              "unreachable stale entry failed for an unrelated reason")

        for name, payload in (("oversized_token", b"0x" + b"1" * 40 + b" 1 4 0x7003\n"),
                              ("nul_token", b"0x1000\x00 1 4 0x7003\n")):
            case = root / name
            case.mkdir()
            covered = case / "covered.txt"
            covered.write_bytes(payload)
            result, out, _ = run_case(exe, root, name + "_run", [0x7003, 0x4E75],
                                      ("0x1000",), (covered,))
            check(result.returncode != 0, f"{name}: malformed token accepted")
            check(no_outputs(out), f"{name}: output written before token rejection")

        missing_case = root / "missing_file"
        missing_case.mkdir()
        missing, missing_out, _ = run_case(exe, root, "missing_file_run",
                                           [0x7003, 0x4E75], ("0x1000",),
                                           (missing_case / "absent.txt",))
        check(missing.returncode != 0, "missing covered-entry file accepted")
        check(no_outputs(missing_out), "missing covered-entry file wrote outputs")

    print("Covered-entry exact guards, duplicate handling, CFG traversal, NULL/count-zero module and pre-output rejection pass")


if __name__ == "__main__":
    main()

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(
    probe: Path,
    output_dir: Path,
    value: str = "1",
    vector_changes: str | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["SEGACD_PROBE_INPUT_SELFTEST"] = "1"
    env["SEGACD_PROBE_PC_ROOTS"] = value
    if vector_changes is not None:
        env["SEGACD_PROBE_VECTOR_CHANGES"] = vector_changes
    else:
        env.pop("SEGACD_PROBE_VECTOR_CHANGES", None)
    return subprocess.run(
        [str(probe), "unused-bios.bin", "0", str(output_dir)],
        env=env, capture_output=True, text=True, timeout=10, check=False,
    )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: probe_pc_roots_test.py SEGACD_BIOS_FLOOR_PROBE")
    probe = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="segacd-pc-roots-") as temporary:
        root = Path(temporary)
        output = root / "valid"
        output.mkdir()
        result = invoke(probe, output)
        if result.returncode:
            raise AssertionError(f"selftest export failed ({result.returncode}): {result.stderr}")

        main_roots = (output / "floor-pc-roots-epoch-000-main.txt").read_text(encoding="ascii")
        sub_roots = (output / "floor-pc-roots-epoch-000-sub.txt").read_text(encoding="ascii")
        if main_roots != "0x123456\n0xFF1000\n":
            raise AssertionError(f"Main roots not sorted/deduplicated: {main_roots!r}")
        if sub_roots != "0x0005E8\n0x00A120\n":
            raise AssertionError(f"Sub roots not sorted: {sub_roots!r}")
        metadata = json.loads((output / "floor-pc-roots-epoch-000.json").read_text(encoding="utf-8"))
        if (metadata["epoch"], metadata["frame"], metadata["sub_clock"],
                metadata["main_count"], metadata["sub_count"]) != (0, 42, 123456, 2, 2):
            raise AssertionError(f"wrong root metadata: {metadata!r}")
        if "nested WAIT" not in metadata["scope"] or "not complete execution coverage" not in metadata["scope"]:
            raise AssertionError("root coverage limitations missing")
        if (output / "sub-vector-changes.jsonl").exists():
            raise AssertionError("vector-change capture should remain disabled by default")

        vector_output = root / "vector-changes"
        vector_output.mkdir()
        vectors = invoke(probe, vector_output, vector_changes="1")
        if vectors.returncode:
            raise AssertionError(f"vector-change selftest failed ({vectors.returncode}): {vectors.stderr}")
        vector_snapshot = vector_output / "sub-vector-changes-000000.bin"
        vector_log = vector_output / "sub-vector-changes.jsonl"
        expected = bytes((n * 17 + 3) & 0xFF for n in range(0x60))
        if vector_snapshot.read_bytes() != expected:
            raise AssertionError("synthetic 96-byte vector snapshot was not written exactly")
        records = [json.loads(line) for line in vector_log.read_text(encoding="utf-8").splitlines()]
        if len(records) != 1 or records[0].get("file") != vector_snapshot.name:
            raise AssertionError(f"unexpected vector-change event log: {records!r}")
        event = records[0]
        if (event["sub_clock_before"], event["sub_clock_after"], event["frame_before"], event["frame_after"],
                event["main_pc_before"], event["main_pc_after"], event["sub_pc_before"], event["sub_pc_after"],
                event["step_status"], event["native_retired_main"], event["floor_retired_sub"]) != (
                100, 117, 1, 2, "FF1234", "FF1236", "005F76", "000660", 0, 1, 1):
            raise AssertionError(f"vector-change step metadata incorrect: {event!r}")
        changed_offsets = [entry["offset"] for entry in event["changes"]]
        expected_offsets = [n for n, value in enumerate(expected) if value]
        if changed_offsets != expected_offsets or event["writer_attribution"].startswith("cpu"):
            raise AssertionError("vector-change byte diffs or ambiguous-writer disclaimer missing")

        invalid_vectors = invoke(probe, vector_output, vector_changes="true")
        if invalid_vectors.returncode == 0 or "SEGACD_PROBE_VECTOR_CHANGES accepts only 1" not in invalid_vectors.stderr:
            raise AssertionError("invalid SEGACD_PROBE_VECTOR_CHANGES value was accepted")

        invalid = invoke(probe, output, "true")
        if invalid.returncode == 0 or "accepts only 1" not in invalid.stderr:
            raise AssertionError("invalid SEGACD_PROBE_PC_ROOTS value was accepted")

        missing = invoke(probe, root / "absent-directory")
        if missing.returncode == 0 or "PC-root export failed" not in missing.stderr:
            raise AssertionError("root output failure was not reported")

    print("segacd probe PC roots PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(probe: Path, output_dir: Path, value: str = "1") -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["SEGACD_PROBE_INPUT_SELFTEST"] = "1"
    env["SEGACD_PROBE_PC_ROOTS"] = value
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

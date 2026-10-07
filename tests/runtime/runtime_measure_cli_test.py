"""Check measurement argument failures before ROM/SDL initialization.

Usage: native-python runtime_measure_cli_test.py path/to/runner.exe
No game or timed workload is executed by this test.
"""
import subprocess
import sys
from pathlib import Path


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    cases = [
        ["--runtime-uncapped"],
        ["--measure-runtime"],
        *[["--measure-runtime", value] for value in
          ("0", "-1", "abc", "1x", "1000001", "4294967296")],
        ["--measure-runtime", "2", "--benchmark", "2"],
        ["--benchmark", "2", "--measure-runtime", "2"],
        ["--measure-runtime", "2", "--turbo"],
        ["--turbo", "--measure-runtime", "2"],
        ["--measure-runtime", "2", "--max-frames", "2"],
    ]
    for args in cases:
        result = subprocess.run([str(binary), *args], capture_output=True,
                                text=True, timeout=5)
        assert result.returncode == 2, (args, result.returncode, result.stderr)
        assert "--measure-runtime" in result.stderr, (args, result.stderr)
        assert "[VIDEO]" not in result.stderr, (args, result.stderr)
    print(f"runtime measurement CLI: {len(cases)} pre-initialization checks passed")


if __name__ == "__main__":
    main()

"""Portable black-box tests for M68kStepRecomp --entries-file handling."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

if len(sys.argv) < 2:
    raise SystemExit("usage: entry_file_test.py PATH_TO_SEGACD_MODULE_RECOMPILER")
GENERATOR = Path(sys.argv[1]).resolve()
sys.argv = [sys.argv[0], *sys.argv[2:]]


class EntryFileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="segacd-entry-file-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.image = self.root / "synthetic-nop-rts.bin"
        self.image.write_bytes(bytes.fromhex("4E71 4E75"))

    def run_generator(self, name, extra):
        output = self.root / name
        output.mkdir()
        result = subprocess.run(
            [str(GENERATOR), str(self.image), str(output), "fixture", "0x1000", *extra],
            capture_output=True,
            text=True,
            check=False,
        )
        return result, output

    @staticmethod
    def output_bytes(directory):
        return {path.name: path.read_bytes() for path in sorted(directory.iterdir()) if path.is_file()}

    def test_positional_and_entries_file_outputs_are_byte_identical(self):
        positional, positional_dir = self.run_generator("positional", ["0x1000"])
        self.assertEqual(positional.returncode, 0, positional.stdout + positional.stderr)
        roots = self.root / "root-one.txt"
        roots.write_bytes(b"0x1000\n")
        file_result, file_dir = self.run_generator("entries-file", ["--entries-file", str(roots)])
        self.assertEqual(file_result.returncode, 0, file_result.stdout + file_result.stderr)
        first = self.output_bytes(positional_dir)
        self.assertEqual(first, self.output_bytes(file_dir))
        self.assertEqual(len(first), 35)

    def test_multiple_files_and_positional_roots_match_positional_baseline(self):
        baseline, baseline_dir = self.run_generator("baseline", ["0x1000", "0x1002"])
        self.assertEqual(baseline.returncode, 0, baseline.stdout + baseline.stderr)
        first = self.root / "root-a.txt"
        second = self.root / "root-b.txt"
        first.write_bytes(b"0x1002\n")
        second.write_bytes(b"0x1000\n")
        mixed, mixed_dir = self.run_generator(
            "mixed", ["0x1000", "--entries-file", str(first), "--entries-file", str(second)]
        )
        self.assertEqual(mixed.returncode, 0, mixed.stdout + mixed.stderr)
        self.assertEqual(self.output_bytes(baseline_dir), self.output_bytes(mixed_dir))

    def test_ten_thousand_duplicate_roots_do_not_require_long_command_line(self):
        baseline, baseline_dir = self.run_generator("baseline", ["0x1000"])
        self.assertEqual(baseline.returncode, 0, baseline.stdout + baseline.stderr)
        roots = self.root / "many-roots.txt"
        roots.write_bytes(b"0x1000\n" * 10000)
        self.assertGreater(roots.stat().st_size, 32767)
        result, output = self.run_generator("many-duplicates", ["--entries-file", str(roots)])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.output_bytes(baseline_dir), self.output_bytes(output))

    def test_invalid_entries_fail_without_creating_outputs(self):
        cases = {
            "missing-file": ("--entries-file", str(self.root / "does-not-exist.txt")),
            "empty-file": ("--entries-file", self.write_case("empty.txt", b"")),
            "whitespace-only": ("--entries-file", self.write_case("spaces.txt", b" \r\n\t")),
            "long-token": ("--entries-file", self.write_case("long.txt", b"0" * 33)),
            "nul-token": ("--entries-file", self.write_case("nul.txt", b"0x1000\0garbage")),
            "garbage": ("--entries-file", self.write_case("garbage.txt", b"0x1000oops")),
            "odd-address": ("--entries-file", self.write_case("odd.txt", b"0x1001")),
            "outside-image": ("--entries-file", self.write_case("outside.txt", b"0x1004")),
            "over-24-bit": ("--entries-file", self.write_case("range.txt", b"0x1000000")),
        }
        for label, args in cases.items():
            with self.subTest(case=label):
                result, output = self.run_generator("invalid-" + label, list(args))
                self.assertNotEqual(result.returncode, 0, label)
                self.assertEqual(list(output.iterdir()), [], label)

    def test_missing_option_path_fails_without_creating_outputs(self):
        result, output = self.run_generator("invalid-missing-option-path", ["--entries-file"])
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(output.iterdir()), [])

    def write_case(self, name, contents):
        path = self.root / name
        path.write_bytes(contents)
        return str(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)

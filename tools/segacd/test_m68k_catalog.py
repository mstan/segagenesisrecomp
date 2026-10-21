"""Focused synthetic tests for the byte-index catalog analyzer/driver."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib

import m68k_catalog as catalog


def make_dispatch_image(base: int, *, pc_offset: int = 0x80,
                        move_disp: int = 0x20, transfer_disp: int = 0x1C,
                        byte_field_disp: int = 0,
                        target_offsets: tuple[int, ...] = (0xA0,)) -> bytes:
    """Build exact MOVEQ/MOVE.B/MOVE.W/indexed-JSR bytes and its table."""
    image = bytearray(0x200)
    pc = base + pc_offset
    # MOVEQ #0,D0; MOVE.B disp(A0),D0. The source displacement is zero.
    words = {pc - 6: 0x7000, pc - 4: 0x1028, pc - 2: byte_field_disp & 0xFFFF,
             pc: 0x323B, pc + 4: 0x4EBB}
    move_extension = ((0 << 12) | (move_disp & 0xFF))
    transfer_extension = ((1 << 12) | (transfer_disp & 0xFF))
    words[pc + 2] = move_extension
    words[pc + 6] = transfer_extension
    read_base = pc + 2 + catalog.signed8(move_extension & 0xFF)
    branch_base = pc + 6 + catalog.signed8(transfer_extension & 0xFF)
    assert read_base == branch_base
    for index, target_offset in enumerate(target_offsets):
        cell = read_base + index * 2
        relative = base + target_offset - branch_base
        words[cell] = relative & 0xFFFF
    if not (target_offsets[0] & 1):
        words[base + target_offsets[0]] = 0x4E75  # RTS target
    for address, value in words.items():
        offset = address - base
        if 0 <= offset <= len(image) - 2:
            image[offset:offset + 2] = value.to_bytes(2, "big")
    return bytes(image)


def row(pc: int) -> dict[str, str]:
    return {"pc": f"{pc:06X}", "opcode": "323B", "words": "2",
            "nominal_cycles": "14", "kind": "native"}


def temporary_test_directory(prefix: str):
    configured = os.environ.get("M68K_CATALOG_TEST_ROOT")
    parent = Path(configured) if configured else None
    if parent is not None:
        parent.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(prefix=prefix, dir=parent)


class ByteIndexAnalysisTests(unittest.TestCase):
    def test_signed_low_byte_and_even_slot_enumeration(self):
        base = 0x200000
        image = make_dispatch_image(base, pc_offset=0x80,
                                    move_disp=0xC0, transfer_disp=0xBC,
                                    target_offsets=(0xA0,))
        pc = base + 0x80
        qualified, unqualified, roots = catalog.analyze_dispatches(image, base, [row(pc)])
        self.assertEqual(unqualified, [])
        self.assertEqual(len(qualified), 1)
        self.assertEqual(int(qualified[0]["table_base"], 16), pc - 0x3E)
        self.assertEqual(sum(len(qualified[0][key]) for key in
                             ("candidate_roots", "odd_target_candidates", "out_of_image_targets",
                              "out_of_image_table_words")), 128)
        self.assertIn(base + 0xA0, roots)
        self.assertEqual(qualified[0]["candidate_roots"][0]["index"], 0)
        self.assertEqual(qualified[0]["candidate_roots"][-1]["index"], 254)

        image = make_dispatch_image(base, pc_offset=0x80,
                                    move_disp=0x20, transfer_disp=0x1C,
                                    byte_field_disp=-2)
        qualified, _, _ = catalog.analyze_dispatches(image, base, [row(pc)])
        self.assertEqual(qualified[0]["state_byte_displacement"], -2)

    def test_register_chain_and_brief_extension_validation(self):
        base = 0x1000
        image = bytearray(make_dispatch_image(base, target_offsets=(0xA0,)))
        pc = base + 0x80
        # An address-register index is not the Dn.W form this proof requires.
        image[pc + 2 - base:pc + 4 - base] = (0x80C0).to_bytes(2, "big")
        qualified, unqualified, _ = catalog.analyze_dispatches(bytes(image), base, [row(pc)])
        self.assertEqual(qualified, [])
        self.assertIn("invalid Dn.W extension", " ".join(unqualified[0]["reasons"]))

        # A valid D7.W is accepted by the extension parser (A/D bit is clear).
        self.assertEqual(catalog.brief_data_word_index(0x70FE), 7)
        self.assertIsNone(catalog.brief_data_word_index(0xF0FE))
        self.assertIsNone(catalog.brief_data_word_index(0x0801))  # long index
        self.assertIsNone(catalog.brief_data_word_index(0x0201))  # scaled index

    def test_out_of_image_table_and_odd_target_are_retained(self):
        base = 0x200000
        pc = base + 0x20
        image = bytearray(make_dispatch_image(base, pc_offset=0x20,
                                              move_disp=0x84, transfer_disp=0x80,
                                              target_offsets=(0xA0,)))
        # This pair shares a table base below the mapped image.
        qualified, _, _ = catalog.analyze_dispatches(bytes(image), base, [row(pc)])
        self.assertGreater(len(qualified[0]["out_of_image_table_words"]), 0)
        self.assertEqual(sum(len(qualified[0][key]) for key in
                             ("candidate_roots", "odd_target_candidates", "out_of_image_targets",
                              "out_of_image_table_words")), 128)

        # Put an odd displacement in the first in-image table slot.
        pc = base + 0x80
        image = bytearray(make_dispatch_image(base, pc_offset=0x80,
                                              move_disp=0x20, transfer_disp=0x1C,
                                              target_offsets=(0xA1,)))
        qualified, _, roots = catalog.analyze_dispatches(bytes(image), base, [row(pc)])
        self.assertEqual(len(qualified[0]["odd_target_candidates"]), 1)
        self.assertEqual(qualified[0]["odd_target_candidates"][0]["target"], f"{base + 0xA1:08X}")
        self.assertNotIn(base + 0xA1, roots)

        # A word with a far signed displacement is retained as an out-of-image
        # target outcome rather than truncated into an in-image root.
        read_base = pc + 2 + 0x20
        image[read_base - base:read_base - base + 2] = (0x7FFE).to_bytes(2, "big")
        qualified, _, roots = catalog.analyze_dispatches(bytes(image), base, [row(pc)])
        self.assertIn(0, [entry["index"] for entry in qualified[0]["out_of_image_targets"]])
        self.assertFalse(any(entry["index"] == 0 for entry in qualified[0]["candidate_roots"]))

    def test_odd_shared_table_base_is_unqualified(self):
        base = 0x200000
        pc = base + 0x80
        image = make_dispatch_image(base, pc_offset=0x80,
                                    move_disp=0x21, transfer_disp=0x1D,
                                    target_offsets=(0xA0,))
        qualified, unqualified, roots = catalog.analyze_dispatches(image, base, [row(pc)])
        self.assertEqual(qualified, [])
        self.assertIn("outside this even-index closure model", " ".join(unqualified[0]["reasons"]))
        self.assertEqual(unqualified[0]["move_table_base"], unqualified[0]["transfer_table_base"])
        self.assertTrue(int(unqualified[0]["move_table_base"], 16) & 1)
        self.assertFalse(roots)

    def test_coverage_validation_rejects_duplicate_pc_and_module_stale_cycles(self):
        base = 0x1000
        image = b"\x4E\x75"
        coverage = {"pc": "001000", "opcode": "4E75", "words": "1",
                    "nominal_cycles": "15", "kind": "native"}
        duplicate_failures = catalog.validate_coverage_rows(image, base, [coverage, coverage])
        self.assertTrue(any("duplicate PC" in " ".join(item.get("errors", []))
                            for item in duplicate_failures))

        crc = zlib.crc32(image) & 0xFFFFFFFF
        module = ("static const M68kStepEntry entries[]={\n"
                  " {0x001000u,{0x4E75},1,16,unit_at_001000},\n};\n"
                  f"const M68kStepModule unit_module={{\"unit\",0x001000u,2u,0x{crc:08X}u,"
                  "entries,sizeof entries/sizeof *entries};\n")
        with temporary_test_directory("m68k-audit-") as temp:
            module_path = Path(temp) / "unit_module.c"
            module_path.write_text(module, encoding="ascii")
            audit = catalog.audit_module(image, base, module_path, [coverage], "unit")
        self.assertTrue(any("disagrees with coverage row" in item.get("error", "")
                            for item in audit["failures"]))


class GeneratorBlackBoxTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("M68K_STEP_RECOMP"),
                         "set M68K_STEP_RECOMP to run the frontend integration test")
    def test_iterates_to_dispatch_target_fixed_point_and_keeps_unsupported(self):
        with temporary_test_directory("m68k-catalog-test-") as temp:
            case = Path(temp)
            base = 0x1000
            image_path = case / "owned.bin"
            image = make_dispatch_image(base, pc_offset=0x80,
                                        move_disp=0x20, transfer_disp=0x1C,
                                        target_offsets=(0xA0,))
            image_path.write_bytes(image)
            roots = case / "roots.txt"
            roots.write_text(f"0x{base + 0x7A:06X}\n", encoding="ascii")
            output = case / "out"
            command = [sys.executable, str(Path(__file__).with_name("m68k_catalog.py")),
                       "--image", str(image_path), "--output-dir", str(output),
                       "--generator", os.environ["M68K_STEP_RECOMP"],
                       "--prefix", "synthetic_byte_index", "--load-base", hex(base),
                       "--entries-file", str(roots)]
            result = subprocess.run(command, capture_output=True, text=True)
            report = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "fixed_point",
                             f"{report.get('failure')}; stdout={result.stdout}; stderr={result.stderr}; "
                             f"generator log={(output / 'iteration-000' / 'generation.log').read_text(errors='replace') if (output / 'iteration-000' / 'generation.log').exists() else 'missing'}")
            self.assertGreaterEqual(len(report["iterations"]), 2)
            self.assertEqual(report["fixed_point"]["unsupported_rows"], 0)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            target = f"0x{base + 0xA0:06X}"
            final_roots = (Path(report["iterations"][-1]["roots_file"])
                           .read_text(encoding="ascii").split())
            self.assertIn(target, final_roots)
            self.assertTrue(Path(report["final_catalog"]).is_dir())

    @unittest.skipUnless(os.environ.get("M68K_STEP_RECOMP"),
                         "set M68K_STEP_RECOMP to run the frontend integration test")
    def test_unsupported_rows_survive_in_manifest_and_fail_closed(self):
        with temporary_test_directory("m68k-catalog-unsupported-") as temp:
            case = Path(temp)
            base = 0x1800
            image_path = case / "owned.bin"
            image_path.write_bytes(b"\x80\xC0\x4E\x75")
            output = case / "out"
            namespace = catalog.build_parser().parse_args([
                "--image", str(image_path), "--output-dir", str(output),
                "--generator", os.environ["M68K_STEP_RECOMP"],
                "--prefix", "synthetic_unsupported", "--load-base", hex(base),
                "--entry", hex(base)])

            def fake_generator(command, **kwargs):
                catalog_dir = Path(command[2])
                prefix = command[3]
                image_bytes = Path(command[1]).read_bytes()
                crc = zlib.crc32(image_bytes) & 0xFFFFFFFF
                (catalog_dir / f"{prefix}_coverage.csv").write_text(
                    "pc,opcode,words,nominal_cycles,kind\n"
                    "001800,80C0,1,140,unsupported\n"
                    "001802,4E75,1,16,native\n", encoding="ascii")
                (catalog_dir / f"{prefix}_module.c").write_text(
                    "static const M68kStepEntry entries[]={\n"
                    " {0x001802u,{0x4E75},1,16,synthetic_at_001802},\n};\n"
                    f"const M68kStepModule {prefix}_module={{\"{prefix}\",0x001800u,4u,"
                    f"0x{crc:08X}u,entries,sizeof entries/sizeof *entries}};\n",
                    encoding="ascii")
                return subprocess.CompletedProcess(command, 3)

            with mock.patch.object(catalog.subprocess, "run", side_effect=fake_generator):
                with self.assertRaises(catalog.CatalogError):
                    catalog.run(namespace)
            report = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "fixed_point_with_unsupported_rows",
                             str(report.get("failure")))
            self.assertGreater(report["fixed_point"]["unsupported_rows"], 0)
            coverage = Path(report["final_catalog"]) / "synthetic_unsupported_coverage.csv"
            self.assertIn(",unsupported", coverage.read_text(encoding="utf-8-sig"))

    @unittest.skipUnless(os.environ.get("M68K_STEP_RECOMP"),
                         "set M68K_STEP_RECOMP to run the frontend integration test")
    def test_truncated_root_is_reported_and_fails(self):
        with temporary_test_directory("m68k-catalog-missing-root-") as temp:
            case = Path(temp)
            base = 0x2000
            image_path = case / "owned.bin"
            image_path.write_bytes(b"\x4E\x75\x20\x3C")  # RTS; truncated MOVE.L #imm,D0
            roots = case / "roots.txt"
            roots.write_text("0x002000\n0x002002\n", encoding="ascii")
            output = case / "out"
            command = [sys.executable, str(Path(__file__).with_name("m68k_catalog.py")),
                       "--image", str(image_path), "--output-dir", str(output),
                       "--generator", os.environ["M68K_STEP_RECOMP"],
                       "--prefix", "synthetic_truncated_root", "--load-base", hex(base),
                       "--entries-file", str(roots)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, "missing requested root must fail closed")
            report = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "failed")
            self.assertEqual(report["root_coverage_audit"]["missing_roots"], ["0x002002"])
            self.assertFalse(report["root_coverage_audit"]["every_requested_root_reported"])


if __name__ == "__main__":
    unittest.main(verbosity=2)

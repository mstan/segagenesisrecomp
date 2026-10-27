"""Reject incomplete, changed or unowned inputs before accepting a catalog."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "segacd"))
import owned_native_generate as generation

GENERATOR = Path(sys.argv.pop(1)).resolve(strict=True)


class OwnedGenerationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="segacd-owned-generation-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    @staticmethod
    def recipe():
        # NOP then RTS. The required guard digest binds PC, instruction bytes,
        # length and nominal cycles, independently of generator file hashes.
        digest = hashlib.sha256()
        for pc, opcode, cycles in ((0x1000, 0x4E71, 4), (0x1002, 0x4E75, 16)):
            digest.update(struct.pack(">IHHH", pc, 1, cycles, opcode))
        return {"cpu": "MAIN", "prefix": "fixture", "load_base": 0x1000,
                "roots_encoding": "ranges", "roots": [[0x1000, 0x1004, 2]],
                "required_entries": 2, "guard_sha256": digest.hexdigest()}

    def test_owned_image_regenerates_exact_required_semantics(self):
        result = generation.generate_image(self.recipe(), bytes.fromhex("4E71 4E75"),
                                           self.root / "valid", GENERATOR, dict(__import__('os').environ))
        self.assertEqual(result["required_entries"], 2)
        self.assertEqual(result["emitted_entries"], 2)

    def test_changed_instruction_fails_required_guard_identity(self):
        # Both inputs are supported and compile successfully. Acceptance must
        # still reject a semantically different source image.
        with self.assertRaisesRegex(generation.RecipeError, "guard semantics changed"):
            generation.generate_image(self.recipe(), bytes.fromhex("4E70 4E75"),
                                      self.root / "changed", GENERATOR, dict(__import__('os').environ))

    def test_resume_rejects_changed_root_addresses_and_modified_callbacks(self):
        row = self.recipe()
        output = self.root / "resume"
        environment = dict(__import__('os').environ)
        generation.generate_image(row, bytes.fromhex("4E71 4E75"), output, GENERATOR, environment)
        generation.generate_image(row, bytes.fromhex("4E71 4E75"), output, GENERATOR, environment, resume=True)
        changed = {**row, "load_base": 0x2000, "roots": [[0x2000, 0x2004, 2]]}
        with self.assertRaisesRegex(generation.RecipeError, "input identity differs"):
            generation.generate_image(changed, bytes.fromhex("4E71 4E75"), output, GENERATOR,
                                      environment, resume=True)
        part = output / "catalog" / "fixture_part00.c"
        part.write_text(part.read_text() + "\n/* altered callback */\n")
        with self.assertRaisesRegex(generation.RecipeError, "source was changed"):
            generation.generate_image(row, bytes.fromhex("4E71 4E75"), output, GENERATOR,
                                      environment, resume=True)

    def test_resume_root_change_is_rejected_with_the_same_load_base(self):
        row = self.recipe()
        row.update(roots_encoding="pc_list", roots=[0x1000, 0x1004])
        records = {0x1000: ((0x4E71,), 4), 0x1004: ((0x4E75,), 16)}
        row["guard_sha256"] = generation.guard_digest(records, row["roots"])
        output = self.root / "same-base-resume"
        environment = dict(__import__('os').environ)
        generation.generate_image(row, bytes.fromhex("4E71 4E71 4E75"), output, GENERATOR, environment)
        changed = {**row, "roots": [0x1002, 0x1004]}
        with self.assertRaisesRegex(generation.RecipeError, "input identity differs"):
            generation.generate_image(changed, bytes.fromhex("4E71 4E71 4E75"), output,
                                      GENERATOR, environment, resume=True)

    def test_no_start_pulse_is_added_to_unspecified_probe_schedule(self):
        bios, disc = self.root / "bios.bin", self.root / "disc.sgcd"
        bios.write_bytes(b"owned-bios")
        disc.write_bytes(b"owned-disc")
        with patch.object(generation.subprocess, "run") as invoke:
            generation.capture_profiles({"capture_profiles": [{"id": "no-start", "subclock_deadline": 1}]},
                                        self.root / "captures", GENERATOR, bios, disc, {})
            command = invoke.call_args.args[0]
            self.assertEqual(len(command), 5, "absence of sixth argv must disable implicit Start")
            self.assertNotIn("SEGACD_PROBE_PAD_EVENTS_OVERRIDE_START", invoke.call_args.kwargs["env"])

    def test_vector_derivation_binds_actual_writer_destination(self):
        bios = self.root / "bios.bin"
        bios.write_bytes(bytes.fromhex("4EF9"))
        writer = bytes.fromhex("21FC 0020 0100 FD08")
        self.root.joinpath("program.bin").write_bytes(writer)
        inventory = {"files": [{"iso_path": "PROGRAM.BIN", "output_path": "program.bin"}]}
        source = {"kind": "derived_vector", "source_base": 0xFFFD06, "bytes": 6,
                  "sha256": generation.sha(bytes.fromhex("4EF9 0020 0100")),
                  "vector_template_source": {"kind": "owned_file", "mapping": "direct-BIOS",
                      "offset": 0, "bytes": 2, "sha256": generation.sha(bios.read_bytes()),
                      "file_sha256": generation.sha(bios.read_bytes())},
                  "writer_source": {"kind": "owned_file", "name": "PROGRAM.BIN", "offset": 0,
                      "bytes": 8, "sha256": generation.sha(writer), "file_sha256": generation.sha(writer)}}
        self.assertEqual(generation.source_image(source, bios, Path("unused"), self.root, inventory, self.root),
                         bytes.fromhex("4EF9 0020 0100"))
        source["source_base"] = 0xFFFD0C
        with self.assertRaisesRegex(generation.RecipeError, "does not target"):
            generation.source_image(source, bios, Path("unused"), self.root, inventory, self.root)

    def test_missing_required_root_and_bad_ranges_are_rejected(self):
        for key, value in (("required_entries", 1), ("roots", [[0x1000, 0x1006, 2]]),
                           ("roots", [[0x1000, 0x1004, 1]])):
            row = self.recipe()
            row[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(generation.RecipeError):
                generation.decode_roots(row, 4)

    def test_source_is_resolved_from_owned_inventory_never_historical_path(self):
        self.root.joinpath("program.bin").write_bytes(bytes.fromhex("4E71 4E75"))
        source = {"kind": "owned_file", "name": "PROGRAM.BIN", "path": "historical/nonexistent.bin",
                  "offset": 0, "bytes": 4, "sha256": generation.sha(bytes.fromhex("4E71 4E75")),
                  "file_sha256": generation.sha(bytes.fromhex("4E71 4E75"))}
        inventory = {"files": [{"iso_path": "PROGRAM.BIN", "output_path": "program.bin"}]}
        data = generation.source_image(source, Path("unused"), Path("unused"), self.root,
                                       inventory, self.root)
        self.assertEqual(data, bytes.fromhex("4E71 4E75"))
        self.root.joinpath("program.bin").write_bytes(bytes.fromhex("4E70 4E75"))
        with self.assertRaisesRegex(generation.RecipeError, "SHA-256 mismatch"):
            generation.source_image(source, Path("unused"), Path("unused"), self.root,
                                    inventory, self.root)

    def test_capture_paths_cannot_escape_profile(self):
        for path in ("../other/input.bin", "C:/private/input.bin", "G:\\private\\input.bin"):
            with self.subTest(path=path), self.assertRaises(generation.RecipeError):
                generation.relative_path(self.root, path)

    def test_old_capture_cannot_be_used_without_current_recipe_dependency(self):
        directory = self.root / "old-profile"
        directory.mkdir()
        directory.joinpath("ram.bin").write_bytes(bytes.fromhex("4E71 4E75"))
        digest = generation.sha(bytes.fromhex("4E71 4E75"))
        source = {"kind": "probe_capture", "profile": "old-profile", "relative_file": "ram.bin",
                  "offset": 0, "bytes": 4, "sha256": digest, "file_sha256": digest}
        with self.assertRaisesRegex(generation.RecipeError, "no verified current profile"):
            generation.source_image(source, Path("unused"), Path("unused"), self.root,
                                    {"files": []}, self.root, {})
        with self.assertRaisesRegex(generation.RecipeError, "not a verified profile artifact"):
            generation.source_image(source, Path("unused"), Path("unused"), self.root,
                                    {"files": []}, self.root, {"old-profile": {"artifacts": []}})


if __name__ == "__main__":
    unittest.main(verbosity=2)

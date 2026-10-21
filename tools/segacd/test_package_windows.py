"""Portable-package tests use synthetic files only; no owned BIOS/disc data."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import tempfile
import unittest

import disc_image
import package_windows


class PortablePackageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="segacd-package-test-")
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.exe = self.source / "candidate.exe"
        self.exe.write_bytes(b"synthetic executable\0")
        self.sdl = self.source / "SDL2.dll"
        self.sdl.write_bytes(b"synthetic SDL runtime\0")
        self.licenses = self.source / "licenses"
        self.licenses.mkdir()
        (self.licenses / "runtime-license.txt").write_text("license text\n", encoding="ascii")
        self.bios = self.source / "external-bios.bin"
        self.bios.write_bytes(b"external BIOS bytes")
        self.disc = self.source / "external-disc.sgcd"
        self.disc.write_bytes(b"external SGCD bytes")
        self.seed = self.source / "blank.brm"
        self.seed.write_bytes(bytes(package_windows.BACKUP_BYTES))
        self.destination = self.root / "package"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def create(self, *, seed: Path | None = None, destination: Path | None = None):
        return package_windows.create_package(argparse.Namespace(
            exe=self.exe,
            sdl2_dll=self.sdl,
            destination=destination or self.destination,
            licenses_dir=self.licenses,
            bios=str(self.bios),
            disc=str(self.disc),
            blank_backup_seed=seed,
        ))

    def test_package_contains_runtime_tools_but_never_copies_owner_media(self):
        result = self.create(seed=self.seed)
        self.assertEqual(result["status"], "created")
        package_windows.validate_package(argparse.Namespace(package=self.destination))
        self.assertEqual((self.destination / "SonicCD.exe").read_bytes(), self.exe.read_bytes())
        self.assertEqual((self.destination / "SDL2.dll").read_bytes(), self.sdl.read_bytes())
        self.assertEqual((self.destination / "seed" / "sonic-cd.brm").read_bytes(), self.seed.read_bytes())
        config = json.loads((self.destination / "userdata" / "portable-config.json").read_text())
        self.assertEqual(config["bios_path"], str(self.bios.resolve()))
        self.assertEqual(config["disc_path"], str(self.disc.resolve()))
        packaged_bytes = [path.read_bytes() for path in self.destination.rglob("*") if path.is_file()]
        self.assertNotIn(self.bios.read_bytes(), packaged_bytes)
        self.assertNotIn(self.disc.read_bytes(), packaged_bytes)
        for relative, source in package_windows.SOURCE_NOTICES.items():
            self.assertEqual((self.destination / relative).read_bytes(),
                             (package_windows.SOURCE_ROOT / source).read_bytes())

    def test_existing_package_is_never_overwritten(self):
        self.create(seed=self.seed)
        save = self.destination / "userdata" / "sonic-cd.brm"
        save.write_bytes(b"later save")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.create(seed=self.seed)
        self.assertEqual(save.read_bytes(), b"later save")

    def test_c_drive_package_destination_is_refused(self):
        self.assertTrue(package_windows.is_c_drive(r"C:\\Games\\SonicCD"))
        self.assertFalse(package_windows.is_c_drive(r"G:\\Games\\SonicCD"))
        with self.assertRaisesRegex(ValueError, "on C: is refused"):
            self.create(destination=Path(r"C:\\Games\\SonicCD"))

    def test_backup_seed_must_have_exact_format_size(self):
        self.seed.write_bytes(b"too short")
        with self.assertRaisesRegex(ValueError, "exactly 8192 bytes"):
            self.create(seed=self.seed)
        self.assertFalse(self.destination.exists())

    def test_validate_detects_modified_runtime_file(self):
        self.create()
        (self.destination / "SDL2.dll").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "hash/size mismatch"):
            package_windows.validate_package(argparse.Namespace(package=self.destination))

    def test_validate_rejects_absolute_and_parent_paths(self):
        for index, unsafe in enumerate(("C:/outside.bin", "../outside.bin")):
            destination = self.root / f"package-unsafe-{index}"
            self.create(destination=destination)
            manifest_path = destination / "package-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["files"][0]["path"] = unsafe
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.subTest(path=unsafe), self.assertRaisesRegex(ValueError, "unsafe"):
                package_windows.validate_package(argparse.Namespace(package=destination))

    def test_validate_requires_explicit_no_media_copy_policy(self):
        self.create()
        manifest_path = self.destination / "package-manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["owner_media_copied"] = True
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "media-copy policy"):
            package_windows.validate_package(argparse.Namespace(package=self.destination))

    def test_cue_cache_identity_tracks_referenced_bin_content(self):
        cue = self.source / "disc.cue"
        raw_track = self.source / "track.bin"
        raw_track.write_bytes(bytes(2352))
        cue.write_text(
            'FILE "track.bin" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n',
            encoding="ascii",
        )
        initial = disc_image.inspect_cue(cue)
        self.assertEqual(initial["schema"], "SGCDCueSourceIdentity1")
        self.assertEqual(initial["sources"][0]["path"], str(raw_track.resolve()))
        self.assertEqual(initial["sources"][0]["sha256"], disc_image.file_sha256(raw_track))
        changed = bytearray(raw_track.read_bytes())
        changed[10] = 1
        raw_track.write_bytes(changed)
        updated = disc_image.inspect_cue(cue)
        self.assertNotEqual(initial["sources"][0]["sha256"], updated["sources"][0]["sha256"])


if __name__ == "__main__":
    unittest.main()

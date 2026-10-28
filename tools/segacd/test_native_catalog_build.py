#!/usr/bin/env python3
"""Focused tests for validating a generated native catalog source bundle."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import argparse

import native_catalog_build as driver


class NativeCatalogBuildTests(unittest.TestCase):
    def test_unknown_compaction_format_keeps_original_registry(self):
        # The ordinary validated fixture deliberately has no native-function
        # prelude. Exercise the real compactor failure, not a mocked exit code.
        args = argparse.Namespace(source_root=Path(__file__).resolve().parents[2],
            catalog_manifest=self.manifest, base_catalogs=self.base, append_overlays=self.overlays,
            bios=self.root / "fixture-bios", disc=self.root / "fixture-disc", build_dir=self.root / "build",
            sdl2_dir=self.root, configure=False, build=False, canonicalize_catalogs=True,
            configuration="Release", parallel=1, cmake=None)
        with mock.patch.object(driver, "validate_owned_inputs", return_value={"fixture": "no media execution"}), \
             mock.patch.object(driver, "git_identity", return_value={"path": "fixture", "revision": "fixture", "dirty": False}):
            result = driver.run(args)
        provenance = json.loads(result["provenance"].read_text(encoding="utf-8"))
        self.assertIsNone(provenance["compact_catalog_manifest"])
        self.assertIn("retaining original registry", provenance["compact_catalog_warning"])
        cache = result["cache"].read_text(encoding="utf-8")
        self.assertIn('set(SEGACD_COMPACT_CATALOG_DIR ""', cache)
        self.assertIn("extra_fixture", cache)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="segacd-native-build-")
        self.root = Path(self.temp.name)
        self.base = self.root / "base.txt"
        self.overlays = self.root / "overlays.txt"
        self.manifest = self.root / "manifest.json"
        self.catalogs = []
        definitions = [("MAIN", "segacd_bios", "base"), ("MAIN", "segacd_main_ram", "base"),
                       ("SUB", "segacd_sub", "base"), ("MAIN", "segacd_boot_main", "base"),
                       ("SUB", "segacd_boot_sub", "base"), ("SUB", "segacd_sub_patch", "base"),
                       ("Z80", "sonic_cd_z80_step", "base"), ("MAIN", "extra_fixture", "overlay")]
        for role, prefix, listed in definitions:
            directory = self.root / prefix
            directory.mkdir()
            if role == "Z80":
                (directory / "sonic_cd_z80_step.c").write_text("int z80_step;\n")
                (directory / "sonic_cd_z80_step.h").write_text("extern int z80_step;\n")
            else:
                (directory / f"{prefix}_module.c").write_text(f"int {prefix}_module;\n")
                (directory / f"{prefix}.h").write_text(f"extern int {prefix}_module;\n")
                for part in range(32):
                    (directory / f"{prefix}_part{part:02d}.c").write_text(f"/* {part} */\n")
            self.catalogs.append((role, directory, prefix, listed))
        for selected, file in (("base", self.base), ("overlay", self.overlays)):
            file.write_text("\n".join(f"{role}|{directory}|{prefix}" for role, directory, prefix, listed
                                       in self.catalogs if listed == selected) + "\n")
        rows = []
        for role, directory, prefix, listed in self.catalogs:
            for source in driver._expected_catalog_files(role, directory, prefix):
                rows.append({"Role": f"{listed}-catalog", "Path": str(source.resolve()),
                             "Sha256": driver.sha256_file(source)})
        self.manifest.write_text(json.dumps({"base_catalogs": [r for r in rows if r["Role"] == "base-catalog"],
                                             "overlay_catalogs": [r for r in rows if r["Role"] == "overlay-catalog"],
                                             "archives": [{"Path": "ignored.lib"}]}, indent=2))

    def tearDown(self):
        self.temp.cleanup()

    def test_valid_bundle_and_cache_keep_z80_special_filename(self):
        bundle = driver.validate_catalog_bundle(self.manifest, self.base, self.overlays)
        self.assertEqual(len(bundle["catalogs"]), 8)
        cache = self.root / "initial-cache.cmake"
        driver.write_initial_cache(cache, bundle["catalogs"], self.root)
        content = cache.read_text()
        self.assertIn("SEGACD_BIOS_PROBE_Z80_MODULE_DIR", content)
        self.assertIn("SEGACD_BIOS_PROBE_EXTRA_MAIN_MODULES", content)
        self.assertIn("extra_fixture", content)

    def test_changed_generated_source_rejected(self):
        (self.catalogs[0][1] / "segacd_bios_module.c").write_text("changed\n")
        with self.assertRaisesRegex(driver.BuildInputError, "hash mismatch"):
            driver.validate_catalog_bundle(self.manifest, self.base, self.overlays)

    def test_manifest_source_omitted_from_lists_rejected(self):
        self.base.write_text(self.base.read_text().replace("MAIN|", "# MAIN|", 1))
        with self.assertRaisesRegex(driver.BuildInputError, "source mismatch|required base catalog"):
            driver.validate_catalog_bundle(self.manifest, self.base, self.overlays)

    def test_bad_catalog_prefix_rejected(self):
        self.overlays.write_text(f"MAIN|{self.root / 'extra_fixture'}|bad-prefix\n")
        with self.assertRaisesRegex(driver.BuildInputError, "invalid role, directory, or prefix"):
            driver.validate_catalog_bundle(self.manifest, self.base, self.overlays)


if __name__ == "__main__":
    unittest.main()

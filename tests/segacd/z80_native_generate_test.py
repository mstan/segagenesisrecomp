"""Contract tests for the strict flat-step Z80 catalog source utility."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "segacd"))
from z80_native_generate import GenerationError, _audit_generated_c, generate  # noqa: E402


FAKE_GENERATOR = r'''import argparse, os, pathlib, re
ap = argparse.ArgumentParser()
ap.add_argument("--game", required=True)
ap.add_argument("--flat-step", action="store_true")
ap.add_argument("--flat-step-variant", action="append", default=[])
a = ap.parse_args()
game = pathlib.Path(a.game)
rom_match = re.search(r'^rom\s*=\s*"([^"]+)"', game.read_text(), re.MULTILINE)
assert rom_match and a.flat_step
base = (game.parent / rom_match.group(1)).read_bytes()
assert len(base) == 16384 and base[:8192] == base[8192:]
images = [base]
for name in a.flat_step_variant:
    data = pathlib.Path(name).read_bytes()
    assert len(data) == 16384 and data[:8192] == data[8192:]
    images.append(data)
if os.environ.get("SEGACD_TEST_CALL_MARKER"):
    pathlib.Path(os.environ["SEGACD_TEST_CALL_MARKER"]).write_text("called:" + os.environ.get("SEGACD_TEST_SENTINEL", "missing"))
root = pathlib.Path(a.game).parent / "generated"
root.mkdir(exist_ok=True)
prefix = "sonic_cd_z80"
with (root / f"{prefix}_step.c").open("w", encoding="utf-8") as f:
    f.write('/* generated */\n#include "sonic_cd_z80_step.h"\n')
    f.write('const uint32_t sonic_cd_z80_code_crc32 = 0x12345678u;\n')
    f.write('const uint32_t sonic_cd_z80_code_size = 0x4000u;\n')
    f.write('void sonic_cd_z80_step(void) { Z80State *s=&g_z80; switch (s->pc) {\n')
    for pc in range(0x4000):
        f.write(f"case 0x{pc:04X}: {{\n")
        if not os.environ.get("SEGACD_TEST_MISS_ONLY"):
            for image in images:
                f.write(f"if (sms_read8(0x{pc:04X}) == 0x{image[pc]:02X}) {{ s->pc = 0x{(pc + 1) & 0x3FFF:04X}; return; }}\n")
        else:
            f.write(f"/* sms_read8(0x{pc:04X}) == 0x{base[pc]:02X} */\n")
        f.write(f"sms_dispatch_miss(0x{pc:04X}); return; }}\n")
    f.write('} }\n')
(root / f"{prefix}_step.h").write_text("void sonic_cd_z80_step(void);\n", encoding="utf-8")
'''


class Z80NativeGenerateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="segacd-z80-generate-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.generator = self.root / "fake_generator.py"
        self.generator.write_text(FAKE_GENERATOR, encoding="utf-8")
        self.rows = []
        for index in range(9):
            path = self.root / f"source-{index}.bin"
            block = bytes([index]) * 8192
            path.write_bytes(block)
            self.rows.append({"path": str(path), "sha256": hashlib.sha256(block).hexdigest(), "profile": f"fixture-{index}"})

    def run_generate(self, name, rows=None, extra_env=None):
        output = self.root / name
        marker = self.root / f"called-{name}.txt"
        env = {"SEGACD_TEST_CALL_MARKER": str(marker), "SEGACD_TEST_SENTINEL": "forwarded"}
        env.update(extra_env or {})
        result = generate(rows if rows is not None else self.rows, self.generator, output, env)
        return result, output, marker

    def test_mirrors_hashed_sources_and_audits_complete_strict_dispatch(self):
        manifest, output, marker = self.run_generate("valid")
        self.assertEqual(manifest["source_count"], 9)
        self.assertEqual(manifest["variant_count"], 8)
        self.assertEqual(manifest["output_contract"]["pc_case_count"], 0x4000)
        self.assertEqual(manifest["output_contract"]["strict_miss_pc_count"], 0x4000)
        self.assertEqual(manifest["output_contract"]["input_guarded_pc_count"], 0x4000)
        self.assertTrue(manifest["output_contract"]["every_input_byte_variant_guarded"])
        self.assertEqual(manifest["generated"]["c"]["input_guarded_pc_count"], 0x4000)
        self.assertEqual(Path(manifest["working_directory"]).resolve(), (output / "seed").resolve())
        self.assertFalse(manifest["output_contract"]["interpreter_fallback"])
        self.assertTrue(manifest["generated"]["c"]["no_interpreter_or_fallback_calls"])
        self.assertTrue((output / "manifest.json").is_file())
        self.assertEqual(marker.read_text(), "called:forwarded")
        for row in manifest["source_rows"]:
            mirrored = Path(row["mirrored_path"]).read_bytes()
            self.assertEqual(len(mirrored), 16384)
            self.assertEqual(mirrored[:8192], mirrored[8192:])
        written = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(written["generated"]["c"]["sha256"], manifest["generated"]["c"]["sha256"])

    def test_hash_mismatch_is_rejected_before_generator_runs(self):
        rows = [dict(self.rows[0])]
        rows[0]["sha256"] = "0" * 64
        marker = self.root / "must-not-run-hash.txt"
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            generate(rows, self.generator, self.root / "bad-hash", {"SEGACD_TEST_CALL_MARKER": str(marker)})
        self.assertFalse(marker.exists())

    def test_wrong_size_is_rejected_before_generator_runs(self):
        wrong = self.root / "short.bin"
        wrong.write_bytes(b"x" * 8191)
        row = {"path": str(wrong), "sha256": hashlib.sha256(wrong.read_bytes()).hexdigest()}
        marker = self.root / "must-not-run-size.txt"
        with self.assertRaisesRegex(ValueError, "exactly 8192 bytes"):
            generate([row], self.generator, self.root / "bad-size", {"SEGACD_TEST_CALL_MARKER": str(marker)})
        self.assertFalse(marker.exists())

    def test_rejects_more_than_eight_variants(self):
        with self.assertRaisesRegex(ValueError, "one base plus at most 8 variants"):
            generate(self.rows + [self.rows[0]], self.generator, self.root / "too-many")

    def test_rejects_nonempty_output_to_prevent_stale_catalog_mix(self):
        output = self.root / "existing"
        output.mkdir()
        (output / "stale.c").write_text("stale")
        with self.assertRaisesRegex(ValueError, "must be new or empty"):
            generate(self.rows[:1], self.generator, output)

    def test_audit_rejects_missing_pc_or_strict_miss(self):
        _, output, _ = self.run_generate("audit-mutations", self.rows[:1])
        c_path = output / "seed" / "generated" / "sonic_cd_z80_step.c"
        original = c_path.read_text(encoding="utf-8")
        c_path.write_text(original.replace("case 0x3FFF:", "case 0xFFFF:", 1), encoding="utf-8")
        with self.assertRaisesRegex(GenerationError, "exactly 0x4000 PCs"):
            _audit_generated_c(c_path)
        c_path.write_text(original.replace("sms_dispatch_miss(0x1234);", "return;", 1), encoding="utf-8")
        with self.assertRaisesRegex(GenerationError, "strict miss for every PC"):
            _audit_generated_c(c_path)

    def test_audit_rejects_interpreter_fallback_reference(self):
        _, output, _ = self.run_generate("fallback-audit", self.rows[:1])
        c_path = output / "seed" / "generated" / "sonic_cd_z80_step.c"
        with c_path.open("a", encoding="utf-8") as stream:
            stream.write("\nvoid z80_interpreter_fallback(void);\nvoid extra(void) { z80_interpreter_fallback(); }\n")
        with self.assertRaisesRegex(GenerationError, "interpreter/fallback call"):
            _audit_generated_c(c_path)

    def test_generation_rejects_all_miss_dispatch_even_when_every_pc_is_listed(self):
        marker = self.root / "miss-only-was-called.txt"
        with self.assertRaisesRegex(GenerationError, "lacks input-byte native guards"):
            generate(self.rows[:1], self.generator, self.root / "miss-only", {"SEGACD_TEST_MISS_ONLY": "1", "SEGACD_TEST_CALL_MARKER": str(marker)})
        self.assertTrue(marker.exists())


if __name__ == "__main__":
    unittest.main()

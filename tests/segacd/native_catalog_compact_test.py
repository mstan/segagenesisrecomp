"""Exercise priority, full guards and conservative operation equivalence."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[2] / "tools/native_catalog_compact.py"
spec = importlib.util.spec_from_file_location("native_catalog_compact", TOOL)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class CompactTests(unittest.TestCase):
    def catalog(self, root, prefix, rows, body=" ctx->state.regs.D[0]++;\n"):
        directory = root / prefix
        directory.mkdir()
        operations, entries, declarations = [], [], []
        for pc, words, cycles in rows:
            symbol = f"{prefix}_at_{pc:06X}"
            operations.append(f"void {symbol}(M68kStepContext *ctx){{\n{body}}}\n")
            declarations.append(f"extern void {symbol}(M68kStepContext *);\n")
            entries.append(f" {{0x{pc:06X}u,{{{','.join(hex(w) for w in words)}}},{len(words)},{cycles},{symbol}}},\n")
        (directory / (prefix + "_part00.c")).write_text('#include "m68k_step_bind.h"\n' + "".join(operations), encoding="utf-8")
        (directory / (prefix + "_module.c")).write_text('#include "' + prefix + '.h"\n' + "".join(declarations) +
            'static const M68kStepEntry entries[]={\n' + "".join(entries) + '};\nconst M68kStepModule ' + prefix +
            '_module={"' + prefix + '",0u,65536u,0x00000000u,entries,sizeof entries/sizeof *entries};\n', encoding="utf-8")
        return f"MAIN|{directory}|{prefix}\n"

    def test_exact_duplicates_and_variant_priority(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            rows = [(0x100, [0x1234, 0x5678], 8), (0x200, [0x4E71], 4)]
            order = self.catalog(root, "first", rows)
            order += self.catalog(root, "duplicate", rows)
            order += self.catalog(root, "extension", [(0x100, [0x1234, 0x9ABC], 8)])
            order += self.catalog(root, "cycles", [(0x100, [0x1234, 0x5678], 12)])
            order += self.catalog(root, "operation", [(0x100, [0x1234, 0x5678], 8)], " ctx->state.regs.D[0]--;\n")
            path = root / "ordered.txt"
            path.write_text(order, encoding="utf-8")
            result = tool.compact(path, root / "out", parts=1, chunk_entries=1)
            self.assertEqual(result["groups"]["MAIN"]["input_entries"], 7)
            self.assertEqual(result["groups"]["MAIN"]["unique_entries"], 5)
            records = []
            for name in result["outputs"]:
                if name.endswith(".c"):
                    text = (root / "out" / name).read_text(encoding="utf-8")
                    records.extend(tool.ENTRY.findall(text))
            at100 = [r for r in records if r[0] == "000100"]
            self.assertEqual([r[1] for r in at100], ["0x1234,0x5678", "0x1234,0x9ABC", "0x1234,0x5678", "0x1234,0x5678"])
            self.assertEqual([r[3] for r in at100], ["8", "8", "12", "8"])
            self.assertEqual([r[4] for r in at100], ["first_at_000100"] * 3 + ["operation_at_000100"])
            manifest = json.loads((root / "out/native_catalog_manifest.json").read_text(encoding="utf-8"))
            for name, expected in manifest["outputs"].items():
                self.assertEqual(tool.digest((root / "out" / name).read_bytes()), expected)

    def test_location_static_and_unknown_translation_units_retain_identity(self):
        for body in [" (void)__func__;\n", " static int count; count++;\n", ' (void)"text";\n', " (void)__LINE__;\n"]:
            text = '#include "m68k_step_bind.h"\nvoid p_at_000100(M68kStepContext *ctx){\n' + body + '}\n'
            self.assertEqual(list(tool.operations(text, "p")), [("p_at_000100", None)])
        with self.assertRaises(tool.CatalogError):
            list(tool.operations('#define PRIVATE 1\n#include "m68k_step_bind.h"\n', "p"))

    def test_unknown_module_effects_and_invalid_guards_rejected(self):
        with self.assertRaises(tool.CatalogError):
            tool.entries('#include "p.h"\nstatic int extra;\n', "p", {})
        with self.assertRaises(tool.CatalogError):
            tool.entries('#include "p.h"\nstatic const M68kStepEntry entries[]={\n {0x100u,{0x1234},2,8,m68k_step_illegal},\n};\n', "p", {})


if __name__ == "__main__":
    unittest.main()

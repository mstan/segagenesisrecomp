"""Regression checks for annotation provenance, include depth, and code/data."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sonic_disassembly", Path(__file__).resolve().parents[2] / "tools/sonic_disassembly.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Symbols(unittest.TestCase):
    def test_nested_code_is_kept_but_equates_data_and_wrong_bytes_are_not(self):
        rom = bytearray(0x220)
        rom[0x200:0x20c] = bytes.fromhex("4E75 4E71 1234 4E75 0000 4E75")
        listing = """
 1/ 200 :                     EntryPoint:
 2/ 200 : 4E75                rts
(1) 3/ 202 :                  IncludedCode:
(1) 4/ 202 : 4E71             nop
 5/ 204 :                     Data:
 6/ 204 : 1234                dc.w $1234
(2) 7/ 206 :                  NestedCode:
(2) 8/ 206 : 4E75             rts
 9/ 208 :                     WrongRevision:
 10/ 208 : 4E75               rts
 11/ 20A : =$20A              Constant = $20A
 12/ 20A : 4E75               rts
 13/ 20A :                     loc_20A:
 14/ 100 :                     Z80Label:
 15/ 100 : C9                  ret
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.lst"
            path.write_text(listing)
            self.assertEqual(module.symbols(path, bytes(rom)), {
                0x200: ["EntryPoint"], 0x202: ["IncludedCode"], 0x206: ["NestedCode"]})


if __name__ == "__main__":
    unittest.main()

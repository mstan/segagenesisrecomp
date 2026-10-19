"""A local inline label must retain the first PC-relative table entry/base."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest


class InlineTables(unittest.TestCase):
    def test_local_label_keeps_relative_base(self):
        tool = Path(os.environ.get("GENESIS_TABLE_EXTRACTOR", str(Path(__file__).with_name("gen_disasm_jumptables.py"))))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            listing = root / "source.lst"
            listing.write_text("""(1) 1/ 200 : 0004 .index: dc.w .first-.index
(1) 2/ 202 : 0006 dc.w .second-.index
(1) 3/ 204 : .first:
(1) 4/ 204 : 4E75 rts
(1) 5/ 206 : .second:
(1) 6/ 206 : 4E75 rts
""")
            code = root / "code.txt"
            code.write_text("000204\n000206\n")
            output = root / "tables.toml"
            subprocess.run([sys.executable, str(tool), "--lst", str(listing),
                            "--code-addrs", str(code), "--prefix", "fixture", "--rom-max", "0x1000",
                            "-o", str(output)], check=True, capture_output=True,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            data = tomllib.loads(output.read_text())
            self.assertEqual(data["jump_table"], [dict(start=0x200, end=0x204, stride=2, format="pcrel16")])
            self.assertEqual(data["functions"]["extra"], [0x204, 0x206])


if __name__ == "__main__":
    unittest.main()

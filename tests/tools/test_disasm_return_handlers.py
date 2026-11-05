"""An object handler installed from a return PC needs its own dispatch entry."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('labels', Path(__file__).with_name('gen_disasm_labels.py'))
labels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(labels)


class ReturnHandlers(unittest.TestCase):
    def test_signed_short_call_and_truncated_word_call(self):
        self.assertEqual(labels.static_call_target(0x100, bytes.fromhex('61fe')), 0x100)
        self.assertEqual(labels.static_call_target(0x100, bytes.fromhex('6180')), 0x82)
        self.assertIsNone(labels.static_call_target(0x100, bytes.fromhex('6100')))

    def scan(self, call, continuation='7000 moveq #0,d0', helper='2097 move.l (sp),(a0)'):
        raw = bytes.fromhex(call.split(' jsr')[0].split(' bsr')[0])
        following = 0x100 + len(raw)
        text = f'''1/ 100 : caller:
2/ 100 : {call}
3/ {following:X} : {continuation}
4/ {following+2:X} : 4E75 rts
5/ 400 : camera_helper:
6/ 400 : 4A40 tst.w d0
7/ 402 : 6704 beq.s out
8/ 404 : {helper}
9/ 406 : 4E75 rts
10/ 408 : out:
11/ 408 : 4E75 rts
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.lst'
            path.write_text(text, encoding='utf-8')
            return labels.scan_lst(str(path), 0x1000), following

    def test_installed_continuation_is_callable(self):
        for call in ('4EB9 0000 0400 jsr (camera_helper).l',
                     '4EB8 0400 jsr (camera_helper).w',
                     '4EBA 02FE jsr camera_helper(pc)',
                     '6100 02FE bsr.w camera_helper'):
            with self.subTest(call=call):
                found, pc = self.scan(call)
                self.assertTrue(found[pc][1])
                self.assertTrue(found[pc][0].startswith('ReturnHandler_'))

    def test_data_continuation_is_not_an_entry(self):
        found, pc = self.scan('4EB9 0000 0400 jsr (camera_helper).l', '2097 dc.w $2097')
        self.assertNotIn(pc, found)

    def test_other_store_is_not_a_handler_installation(self):
        found, pc = self.scan('4EB9 0000 0400 jsr (camera_helper).l', helper='2080 move.l d0,(a0)')
        self.assertNotIn(pc, found)

    def test_store_after_return_is_not_reachable(self):
        instructions = {0x100: bytes.fromhex('4eb900000400'), 0x106: bytes.fromhex('7000'),
                        0x400: bytes.fromhex('4e75'), 0x402: bytes.fromhex('2097')}
        self.assertEqual(labels.return_handler_entries(instructions), {})

    def test_assembler_hex_padding_does_not_consume_dbf(self):
        text = '''1/ 100 : 4EB9 0000 0400           \tjsr (helper).l
2/ 106 : 7000                     \tmoveq #0,d0
3/ 108 : 4E75                     \trts
4/ 400 : 51C9 FFFE                \tdbf d1,helper
5/ 404 : 2097                     \tmove.l (sp),(a0)
6/ 406 : 4E75                     \trts
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.lst'
            path.write_text(text, encoding='utf-8')
            self.assertTrue(labels.scan_lst(str(path), 0x1000)[0x106][1])


if __name__ == '__main__':
    unittest.main()

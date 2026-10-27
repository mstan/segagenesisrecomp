"""A runner may exit zero after a rollback diagnostic; the timing gate must fail."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from paired_benchmark import parse_rollback_receipt, verify_hashes

class RollbackGate(unittest.TestCase):
    def test_complete_receipt(self):
        self.assertEqual(parse_rollback_receipt('rb_probe: summary passes=749 divergences=0 asymmetric=0 period=8 depth=8\n')['passes'],749)
    def test_diagnostic_failure_and_absent_work_rejected(self):
        for log in ['', 'rb_probe: summary passes=0 divergences=0 asymmetric=0 period=8 depth=8',
                    'rb_probe: summary passes=2 divergences=1 asymmetric=0 period=8 depth=8',
                    'rb_probe: summary passes=2 divergences=0 asymmetric=1 period=8 depth=8',
                    'rb_probe: summary passes=2 divergences=0 asymmetric=0 period=8 depth=8\n'*2]:
            with self.subTest(log=log),self.assertRaises(RuntimeError):parse_rollback_receipt(log)
    def test_work_mismatch_even_with_identical_final_state(self):
        a=dict(state_fnv1a64='same',audio_state_fnv1a64='same',rollback={'passes':1})
        b=dict(a,rollback={'passes':2})
        with self.assertRaises(RuntimeError):verify_hashes(a,b,'test',False)
if __name__=='__main__':unittest.main()

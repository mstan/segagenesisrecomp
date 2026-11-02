"""Regenerate synthetic guarded variants with the real SmsRecomp producer.

Usage: python generate_fixture.py /path/to/SmsRecomp[.exe] <producer-commit>
The inputs are synthetic; no ROM/captured driver data is committed.
"""
import hashlib,json,re,subprocess,sys,tempfile
from pathlib import Path
out=Path(__file__).resolve().parent/'fixture'
base=bytearray(64)
for at,code in {0:'3e11',8:'320040',16:'3a0040',24:'220040',32:'2a0040',
                40:'dd8600',48:'fd9600',56:'04',58:'013412'}.items():
    base[at:at+len(bytes.fromhex(code))]=bytes.fromhex(code)
variant=base.copy();variant[56]=5
if len(sys.argv)!=3 or not re.fullmatch(r'[0-9a-fA-F]{40}',sys.argv[2]):
    raise SystemExit('Require generator executable and explicit full producer source commit; binary SHA is also retained')
exe=Path(sys.argv[1]).resolve();out.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='genesis-z80-fixture-') as folder:
    root=Path(folder);(root/'base.bin').write_bytes(base);(root/'variant.bin').write_bytes(variant)
    config=root/'game.toml';config.write_text('[game]\noutput_prefix="proof_variants"\nplatform="sms"\nrom="base.bin"\n')
    command=[str(exe),'--game',str(config),'--flat-step','--flat-step-variant',str(root/'variant.bin')]
    result=subprocess.run(command,cwd=root,capture_output=True,text=True,check=True)
    generated=root/'generated'
    # The producer places output next to the TOML.
    for name in ['proof_variants_step.c','proof_variants_step.h']:
        (out/name).write_bytes((generated/name).read_bytes().replace(b'\r\n',b'\n'))
    receipt={'producer_source_commit':sys.argv[2],
             'producer_binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
             'base_hex':base.hex(),'variant_hex':variant.hex(),
             'command':['SmsRecomp','--game','game.toml','--flat-step','--flat-step-variant','variant.bin'],
             'canonical_newlines':'LF','stdout':result.stdout,'outputs':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.?')}}
    (out/'provenance.json').write_bytes((json.dumps(receipt,indent=2)+'\n').encode())

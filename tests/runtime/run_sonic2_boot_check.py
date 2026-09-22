"""Run the existing boot_smoke probe in an isolated, serial native process."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import tomllib
p=argparse.ArgumentParser(description=__doc__)
for name in ('exe','rom','baseline','out'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args(); root=Path(__file__).resolve().parents[2]
out=a.out.resolve(); out.mkdir(parents=True,exist_ok=False)
for source in (a.exe,a.exe.parent/'SDL2.dll'): shutil.copy2(source,out/source.name)
(out/'debug.ini').write_text('port=4388\n')
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
with (out/'run.log').open('w') as log:
    child=subprocess.Popen([str(out/a.exe.name),str(a.rom.resolve()),'--no-launcher','--widescreen','off',
        '--max-frames','300','--target-fps','60'],cwd=out,env=env,stdout=log,stderr=log,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    deadline=time.monotonic()+8
    while True:
        if child.poll() is not None: raise RuntimeError('Boot runner exited early')
        try:
            with socket.create_connection(('127.0.0.1',4388),timeout=.2): pass
            break
        except OSError:
            if time.monotonic()>=deadline: raise
            time.sleep(.1)
    result=subprocess.run([sys.executable,str(root/'tools/boot_smoke.py'),'--game','sonic2',
        '--port','4388','--baseline',str(a.baseline.resolve())],cwd=root)
    assert child.wait(timeout=15)==0
assert not tomllib.loads((out/'dispatch_misses.toml').read_text()).get('functions',{}).get('extra')
raise SystemExit(result.returncode)

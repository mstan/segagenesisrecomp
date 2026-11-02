#!/usr/bin/env python3
"""A promoted RAM entry is a live resolver/stub edge, not a native ROM proof."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
import test_stack_skip as support

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--recompiler',type=Path,required=True)
 comp=ap.add_mutually_exclusive_group(required=True);comp.add_argument('--cmake',type=Path);comp.add_argument('--cc',type=Path)
 args=ap.parse_args();root=Path(__file__).resolve().parents[2]
 with tempfile.TemporaryDirectory(prefix='genesis-ram-edges-') as temp:
  td=Path(temp);rom=bytearray(b'\xff'*0x400);rom[:8]=bytes.fromhex('00fffe00 00000200');rom[0x100:0x200]=b' '*256;rom[0x100:0x104]=b'SEGA'
  rom[0x200:0x206]=bytes.fromhex('4ef9 00ff1000')
  rom[0x240:0x24a]=bytes.fromhex('4eb9 00ff1000 7007 4e75')
  for pc in [0x280,0x2c0,0x2e0,0x2f0]:rom[pc:pc+2]=bytes.fromhex('4e75')
  (td/'test.bin').write_bytes(rom);(td/'game.toml').write_text(support.CONFIG,encoding='utf8')
  harness=support.HARNESS.split('int main(')[0].replace('uint32_t recomp_resolve_ram_trampoline(uint32_t a) { return a; }','uint32_t recomp_resolve_ram_trampoline(uint32_t a) { return a==0xff1000u?0x800000u:a; }')
  harness+='''int main(int argc,char**argv){(void)argc;g_cpu.A[7]=0xfffe00;g_cpu.SR=0x2700;recomp_call_addr(atoi(argv[1])?0x240:0x200);if(dynamic_visits!=1||g_cpu.A[7]!=0xfffe00||pending){fprintf(stderr,"RAM edge skipped live resolver/stub: visits=%u sp=%x pending=%d\\n",dynamic_visits,g_cpu.A[7],pending);return 1;}return 0;}\n'''
  (td/'harness.c').write_text(harness,encoding='utf8')
  support.run([args.recompiler.resolve(),'test.bin','--game','game.toml','--output-dir','generated'],td)
  source='\n'.join(p.read_text(encoding='utf8') for p in (td/'generated').glob('skip_part*.c'))
  assert 'void func_FF1000(' in source,'fixture did not promote the RAM entry'
  sources=sorted((td/'generated').glob('skip_part*.c'))+[td/'generated/skip_dispatch.c',td/'harness.c']
  if args.cmake:
   listing='\n'.join(' "'+p.as_posix()+'"' for p in sources)
   (td/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(RamEdges C)\nadd_executable(edges\n'+listing+'\n)\ntarget_include_directories(edges PRIVATE "'+(root/'runner/include').as_posix()+'" "'+(root/'runner').as_posix()+'")\n',encoding='utf8')
   cmd=[args.cmake.resolve(),'-S',td,'-B',td/'build']
   if os.name=='nt':cmd+=['-G','Visual Studio 17 2022','-A','x64']
   support.run(cmd,td);support.run([args.cmake.resolve(),'--build',td/'build','--config','Release','--parallel','1'],td)
   exe=td/'build'/('Release/edges.exe' if os.name=='nt' else 'edges')
  else:
   exe=td/'edges';support.run([args.cc.resolve(),'-std=c11','-O2','-I',root/'runner/include','-I',root/'runner',*sources,'-o',exe],td)
  failed=[]
  for mode,name in [(0,'promoted static JMP'),(1,'promoted static JSR')]:
   proc=subprocess.run([str(exe),str(mode)],cwd=td,capture_output=True,text=True)
   print(json.dumps({'edge':name,'exit':proc.returncode,'stderr':proc.stderr}),flush=True)
   if proc.returncode:failed.append(name)
  assert not failed,failed
  assert 'RECOMP_TAIL_NATIVE(0xFF1000u' not in source
  assert 'RECOMP_CALL_NATIVE(func_FF1000)' not in source
  print('Promoted RAM tail/call entries retain live resolution and stub behavior',flush=True)
if __name__=='__main__':main()

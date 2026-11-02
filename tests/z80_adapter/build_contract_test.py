"""Exercise real shared-helper configuration, defaults, refusals and identity."""
import subprocess,sys,tempfile
from pathlib import Path
engine=Path(__file__).resolve().parents[2];cmake=sys.argv[1];generator=sys.argv[2]
with tempfile.TemporaryDirectory(prefix='genesis z80 build ') as temp:
    root=Path(temp);(root/'generated input').mkdir();source=root/'generated input/proof_step.c';source.write_text('void proof_step(void){}\n')
    (root/'dummy.c').write_text('int compiled_without_native_source(void){return 1;}\n')
    (root/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.21)
project(SharedZ80Contract C)
include("'''+(engine/'cmake/GenesisRecompZ80.cmake').as_posix()+'''")
add_library(probe OBJECT dummy.c)
genesisrecomp_z80_target(probe)
genesisrecomp_z80_target(probe)
get_target_property(sources probe SOURCES)
get_target_property(definitions probe COMPILE_DEFINITIONS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/contract.txt" "${sources}\\n${definitions}\\n")
''')
    for name,args,error in [
        ('off',[],None),
        ('on',['-DGENESIS_Z80_RECOMP=ON','-DGENESIS_Z80_AOT_SOURCE='+str(source)],None),
        ('missing',['-DGENESIS_Z80_RECOMP=ON'],'needs an existing GENESIS_Z80_AOT_SOURCE'),
        ('entry',['-DGENESIS_Z80_RECOMP=ON','-DGENESIS_Z80_AOT_SOURCE='+str(source),'-DGENESIS_Z80_AOT_ENTRY=not a symbol'],'must name one generated C step function')]:
        build=root/name;argv=[cmake,'-S',str(root),'-B',str(build),'-G',generator,*args]
        if sys.platform=='win32':argv+=['-A','x64']
        result=subprocess.run(argv,capture_output=True,text=True)
        if error:assert result.returncode and error in result.stdout+result.stderr,(name,result.stdout,result.stderr);continue
        assert result.returncode==0,(name,result.stdout,result.stderr)
        report=(build/'contract.txt').read_text()
        if name=='off':
            assert 'z80_recomp.c' not in report and 'GENESIS_Z80_RECOMP' not in report
            subprocess.run([cmake,'--build',str(build),'--config','Release','--parallel','1'],check=True)
        else:
            assert report.count('z80_recomp.c')==1 and report.count('proof_step.c')==1,report
            assert 'GENESIS_Z80_RECOMP=1' in report and 'GENESIS_CONFIGURED_Z80_STEP=proof_step' in report,report
print('Shared native-Z80 build default OFF, real source/idempotence, missing-source and invalid-entry contracts pass')

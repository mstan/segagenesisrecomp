"""Configure the real generated-source helper with caller-owned and legacy inputs.

No ROM, native compiler or game fixture is required; this checks the shared
build contract using a synthetic imported generator, without running it.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cmake', required=True)
parser.add_argument('--generator', required=True)
args = parser.parse_args()
engine = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='genesis caller paths ') as temp:
    root = Path(temp)
    for mode in ('absolute', 'legacy'):
        consumer = root / mode / 'consumer'
        framework = root / mode / 'framework'
        game = consumer / 'game' if mode == 'absolute' else framework / 'fixture'
        game.mkdir(parents=True)
        consumer.mkdir(parents=True, exist_ok=True)
        framework.mkdir(parents=True, exist_ok=True)
        (game / 'game.toml').write_text('[game]\noutput_prefix="fixture"\n')
        game_argument = game.as_posix() if mode == 'absolute' else 'fixture'
        (consumer / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(PathContract NONE)\n'
            'add_executable(GenesisRecomp IMPORTED GLOBAL)\n'
            f'set_target_properties(GenesisRecomp PROPERTIES IMPORTED_LOCATION "{root.as_posix()}/not-run.exe")\n'
            f'set(RECOMP_ROOT "{framework.as_posix()}")\n'
            f'include("{engine.as_posix()}/cmake/GenesisRecompGenerated.cmake")\n'
            f'genesisrecomp_add_generated_sources(OUTPUT fixture "{game_argument}" owner.bin)\n')
        build = root / mode / 'build'
        subprocess.run([args.cmake, '-S', str(consumer), '-B', str(build), '-G', args.generator], check=True)
        options = dict(line.split('=', 1) for line in (build / 'fixture_generation_options.txt').read_text().splitlines())
        assert Path(options['game']) == game, options
        assert Path(options['rom']) == game / 'owner.bin', options
        assert Path(options['input']) == game / 'game.toml', options
        assert options['rom_present'] == '0', options
print('PASS caller-owned absolute paths (including spaces) and unchanged legacy engine-relative paths')

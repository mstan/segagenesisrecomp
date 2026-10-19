"""Build evidence retains tracked dirtiness through unreadable submodule aliases."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cmake', required=True)
    ap.add_argument('--git', required=True)
    args = ap.parse_args()
    script = Path(__file__).resolve().parents[2] / 'cmake/GenesisBuildInfo.cmake'
    with tempfile.TemporaryDirectory(prefix='genesis-build-info-') as tmp:
        root = Path(tmp)

        def git(*cmd):
            return subprocess.check_output([args.git, '-C', str(root), *cmd], stderr=subprocess.STDOUT).decode().strip()

        git('init', '-q')
        git('config', 'user.name', 'Build info test')
        git('config', 'user.email', 'test@example.invalid')
        source = root / 'source.c'
        source.write_text('original\n')
        (root / '.gitmodules').write_text('[submodule "dependency"]\npath = dependency\nurl = invalid\n')
        git('add', 'source.c', '.gitmodules')
        git('commit', '-qm', 'fixture')
        head = git('rev-parse', 'HEAD')
        git('update-index', '--add', '--cacheinfo', f'160000,{head},dependency')
        git('commit', '-qm', 'pinned dependency')
        (root / 'dependency').mkdir()
        (root / 'dependency/.git').write_text('gitdir: /nonexistent/worktree/gitdir\n')
        identity = git('rev-parse', '--short=10', 'HEAD')
        out = root / 'evidence.h'

        def evidence(engine=root):
            subprocess.run([args.cmake, f'-DOUT={out}', f'-DENGINE_DIR={engine}',
                            f'-DGAME_DIR={root}', f'-DGIT={args.git}',
                            '-DGAME_VERSION=0.6.0-cd.experimental.1', '-P', str(script)], check=True)
            return out.read_text()

        clean = evidence()
        assert f'GENESIS_BUILD_ENGINE_GIT "{identity}"' in clean, clean
        assert 'GENESIS_BUILD_GAME_VERSION "0.6.0-cd.experimental.1"' in clean
        source.write_text('changed\n')
        assert f'GENESIS_BUILD_GAME_GIT "{identity}-dirty"' in evidence()
        git('add', 'source.c')
        assert f'GENESIS_BUILD_ENGINE_GIT "{identity}-dirty"' in evidence()
        git('reset', '-q', 'HEAD', '--', 'source.c')
        source.write_text('original\n')
        assert evidence() == clean
        assert 'GENESIS_BUILD_ENGINE_GIT "unknown"' in evidence(root / 'absent')
    print('Build evidence: clean, staged/unstaged dirty, reverted, unavailable and pinned alias cases passed')


if __name__ == '__main__':
    main()

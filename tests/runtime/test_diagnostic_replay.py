"""Record/replay, restore, corruption, interruption and bounded-history checks."""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()
    helper = Path(__file__).resolve().parents[2] / "tools/replay_diagnostic.py"
    spec = importlib.util.spec_from_file_location("replay_diagnostic", helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory(prefix="genesis-replay-test-") as temp:
        root = Path(temp)
        env = dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
        env.pop("GENESIS_REPLAY_RECORD", None)
        def run(*argv, expected=0):
            result = subprocess.run([str(args.fixture.resolve()), *map(str, argv)], env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            assert result.returncode == expected, result.stdout
            return result.stdout
        replay = root / "logs/fixture-feedback.replay"
        run("record", root)
        assert module.inspect(replay)
        assert "all 30 recorded executions matched" in run("play", root, replay)
        original = replay.read_text()
        # A broken state hash is an actual divergence, not a successful replay.
        damaged = original.replace("H 1 ", "H 1 deadbeef ", 1)
        fields = damaged.splitlines()
        for i, line in enumerate(fields):
            if line.startswith("H 1 "):
                words = line.split(); del words[3]; fields[i] = " ".join(words); break
        replay.write_text("\n".join(fields) + "\n", newline="\n")
        output = run("play", root, replay, expected=2)
        assert "FIRST DIVERGENCE" in output, output
        replay.write_text(original.rsplit("E ", 1)[0], newline="\n")
        assert not module.inspect(replay)
        assert "incomplete report" in run("play", root, replay, expected=2)
        # Last accepted input is still executed before its missing H is noticed.
        replay.write_text(original[:original.index("H 30 ")], newline="\n")
        assert "incomplete result after input" in run("play", root, replay, expected=2)
        replay.write_text(original.replace("BUILD 00001234", "BUILD 00004321"), newline="\n")
        run("play", root, replay, expected=2)
        replay.write_text(original, newline="\n")
        state = Path(str(replay) + ".sav")
        attachment = state.read_bytes()
        state.write_bytes(bytes([attachment[0] ^ 1]) + attachment[1:])
        run("play", root, replay, expected=2)
        state.write_bytes(attachment)
        for _ in range(3): run("record", root)
        assert Path(str(replay) + ".previous-1").is_file()
        assert Path(str(replay) + ".previous-2").is_file()
        assert not Path(str(replay) + ".previous-3").exists()
        for old in (replay, Path(str(replay) + ".previous-1"), Path(str(replay) + ".previous-2")):
            assert module.inspect(old)
        # Read-only/unwritable output failure leaves the game fixture running.
        block = root / "blocked"; block.mkdir(); (block / "logs").write_text("occupied")
        run("record", block, expected=2)
    print("diagnostic replay: PASS")


if __name__ == "__main__":
    main()

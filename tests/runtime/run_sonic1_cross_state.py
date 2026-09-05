#!/usr/bin/env python3
"""Run a Sonic 1 save/load compatibility matrix for runtime validation.

The runner's input-script parser tokenizes paths on whitespace, so this script
keeps generated artifacts under build/runtime-validation by default. Those
paths contain no spaces in the normal repo layout.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Iterable


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT_BASE = ROOT / "build" / "runtime-validation"

BAD_LOG_MARKERS = (
    "[load] failed/truncated",
    "[load] empty/missing",
    " is not an own-backend save",
    "[save] failed",
    "[ramdump] failed",
    "[screenshot] failed",
    "dispatch miss: $",
    "[dispatch] interior-label miss:",
    "jsr stack mismatch at",
    "game fiber stack runaway",
)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def copy_runtime(src_exe: Path, dst_dir: Path, rom: Path, linear_filter: int) -> Path:
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst_exe = dst_dir / "SonicTheHedgehogRecomp.exe"
    shutil.copy2(src_exe, dst_exe)

    for name in ("SDL2.dll", "annotations_from_disasm.csv"):
        src = src_exe.parent / name
        if src.exists():
            shutil.copy2(src, dst_dir / name)

    shutil.copy2(rom, dst_dir / "sonic.bin")
    (dst_dir / "settings.ini").write_text(
        "\n".join(
            (
                "[video]",
                "window_scale = 1",
                "fullscreen = 0",
                f"linear_filter = {linear_filter}",
                "widescreen = 0",
                "widescreen_cells = 4",
                "",
                "[audio]",
                "volume = 60",
                "",
                "[launcher]",
                "skip_launcher = 1",
                "",
            )
        ),
        encoding="utf-8",
    )
    return dst_exe


def ensure_script_path_safe(path: Path) -> None:
    text = str(path)
    if any(ch.isspace() for ch in text):
        raise SystemExit(f"generated path contains whitespace and cannot be used by input_script: {text}")


def write_script(path: Path, lines: Iterable[str]) -> None:
    ensure_script_path_safe(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def run_game(
    label: str,
    exe: Path,
    script: Path,
    log_path: Path,
    max_frames: int,
    env_extra: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    cmd = [
        str(exe),
        "--no-launcher",
        "--max-frames",
        str(max_frames),
        "--input-script",
        str(script),
        str(exe.parent / "sonic.bin"),
    ]
    env = os.environ.copy()
    env["SDL_MAIN_HANDLED"] = "1"
    if env_extra:
        env.update(env_extra)
    proc = subprocess.run(
        cmd,
        cwd=exe.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=180,
        env=env,
    )
    log_path.write_text(proc.stdout, encoding="utf-8", errors="replace")
    print(f"{label}: exit={proc.returncode} log={log_path}")
    return proc


def require_log(log_path: Path, required: Iterable[str]) -> list[str]:
    text = log_path.read_text(encoding="utf-8", errors="replace")
    problems: list[str] = []
    for marker in required:
        if marker not in text:
            problems.append(f"missing log marker {marker!r} in {log_path}")
    lower = text.lower()
    for marker in BAD_LOG_MARKERS:
        if marker in lower:
            problems.append(f"bad log marker {marker!r} in {log_path}")
    return problems


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--baseline-exe",
        default=r"F:\Projects\segagenesisrecomp\SonicTheHedgehogRecomp\build-codex-baseline\Release\SonicTheHedgehogRecomp.exe",
        type=Path,
    )
    ap.add_argument(
        "--candidate-exe",
        default=r"F:\Projects\segagenesisrecomp\SonicTheHedgehogRecomp\build-codex-perf\Release\SonicTheHedgehogRecomp.exe",
        type=Path,
    )
    ap.add_argument(
        "--rom",
        default=r"F:\Projects\segagenesisrecomp\SonicTheHedgehogRecomp\Sonic the Hedgehog (JUE) [!].bin",
        type=Path,
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        help="Fresh output directory. If omitted, a timestamped directory is created under build/runtime-validation.",
    )
    ap.add_argument("--skip-linear", action="store_true")
    ap.add_argument("--skip-env-quality-override", action="store_true")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    baseline_exe = args.baseline_exe.resolve()
    candidate_exe = args.candidate_exe.resolve()
    rom = args.rom.resolve()
    if args.out_dir is None:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        out_dir = (DEFAULT_OUT_BASE / f"sonic1-cross-state-{stamp}").resolve()
    else:
        out_dir = args.out_dir.resolve()

    for path in (baseline_exe, candidate_exe, rom):
        if not path.exists():
            raise SystemExit(f"missing required input: {path}")

    if out_dir.exists():
        raise SystemExit(
            f"output directory already exists; choose a fresh --out-dir to preserve prior artifacts: {out_dir}"
        )
    scripts_dir = out_dir / "scripts"
    logs_dir = out_dir / "logs"
    saves_dir = out_dir / "saves"
    dumps_dir = out_dir / "dumps"
    for d in (scripts_dir, logs_dir, saves_dir, dumps_dir):
        d.mkdir(parents=True, exist_ok=True)

    baseline_runtime = copy_runtime(baseline_exe, out_dir / "runtime-baseline-nearest", rom, 0)
    candidate_runtime = copy_runtime(candidate_exe, out_dir / "runtime-candidate-nearest", rom, 0)
    candidate_linear_runtime = None
    if not args.skip_linear:
        candidate_linear_runtime = copy_runtime(candidate_exe, out_dir / "runtime-candidate-linear", rom, 1)

    baseline_save = saves_dir / "baseline.sav"
    candidate_save = saves_dir / "candidate.sav"
    for p in (baseline_save, candidate_save):
        ensure_script_path_safe(p)

    save_specs = (
        ("baseline_save", baseline_runtime, baseline_save),
        ("candidate_save", candidate_runtime, candidate_save),
    )
    runs: list[tuple[str, subprocess.CompletedProcess[str], Path, list[str]]] = []

    for label, exe, save_path in save_specs:
        script = scripts_dir / f"{label}.input"
        write_script(
            script,
            (
                "WAIT 420",
                "PRESS START 2",
                "WAIT 900",
                f"SAVE_STATE {save_path}",
                "WAIT 120",
                "EXIT 0",
            ),
        )
        log = logs_dir / f"{label}.log"
        proc = run_game(label, exe, script, log, 2200)
        runs.append((label, proc, log, [f"[SAVE] saved {save_path}"]))

    load_specs = (
        ("baseline_loads_baseline", baseline_runtime, baseline_save),
        ("candidate_loads_baseline", candidate_runtime, baseline_save),
        ("baseline_loads_candidate", baseline_runtime, candidate_save),
        ("candidate_loads_candidate", candidate_runtime, candidate_save),
    )
    if candidate_linear_runtime is not None:
        load_specs = load_specs + (("candidate_linear_loads_baseline", candidate_linear_runtime, baseline_save),)
    if not args.skip_env_quality_override:
        load_specs = load_specs + (
            ("candidate_config0_env_quality1_loads_baseline", candidate_runtime, baseline_save),
        )

    artifacts: dict[str, tuple[Path, Path, Path]] = {}
    for label, exe, save_path in load_specs:
        initial_ram = dumps_dir / f"{label}.initial.ram"
        final_ram = dumps_dir / f"{label}.final.ram"
        final_png = dumps_dir / f"{label}.final.png"
        for p in (save_path, initial_ram, final_ram, final_png):
            ensure_script_path_safe(p)
        script = scripts_dir / f"{label}.input"
        write_script(
            script,
            (
                "WAIT 30",
                f"LOAD_STATE {save_path}",
                "WAIT 5",
                f"DUMP_RAM {initial_ram}",
                "WAIT 300",
                "PRESS RIGHT 60",
                "PRESS A 10",
                "WAIT 300",
                f"DUMP_RAM {final_ram}",
                f"SCREENSHOT {final_png}",
                "WAIT 5",
                "EXIT 0",
            ),
        )
        log = logs_dir / f"{label}.log"
        env_extra = {"SDL_RENDER_SCALE_QUALITY": "1"} if label == "candidate_config0_env_quality1_loads_baseline" else None
        proc = run_game(label, exe, script, log, 2200, env_extra=env_extra)
        artifacts[label] = (initial_ram, final_ram, final_png)
        runs.append(
            (
                label,
                proc,
                log,
                [
                    f"[LOAD] loaded {save_path}",
                    f"[RAMDUMP] wrote {initial_ram}",
                    f"[RAMDUMP] wrote {final_ram}",
                    f"[SCREENSHOT] wrote {final_png}",
                ],
            )
        )

    problems: list[str] = []
    for label, proc, log, required in runs:
        if proc.returncode != 0:
            problems.append(f"{label} exited {proc.returncode}")
        problems.extend(require_log(log, required))

    for save in (baseline_save, candidate_save):
        if not save.exists() or save.stat().st_size == 0:
            problems.append(f"save missing or empty: {save}")

    for label, (initial_ram, final_ram, final_png) in artifacts.items():
        for path in (initial_ram, final_ram, final_png):
            if not path.exists() or path.stat().st_size == 0:
                problems.append(f"artifact missing or empty for {label}: {path}")
        if initial_ram.exists() and final_ram.exists() and sha256(initial_ram) == sha256(final_ram):
            problems.append(f"RAM did not change after post-load input for {label}")

    comparisons = (
        ("baseline-save RAM", "baseline_loads_baseline", "candidate_loads_baseline", 1),
        ("baseline-save screenshot", "baseline_loads_baseline", "candidate_loads_baseline", 2),
        ("candidate-save RAM", "baseline_loads_candidate", "candidate_loads_candidate", 1),
        ("candidate-save screenshot", "baseline_loads_candidate", "candidate_loads_candidate", 2),
    )
    print("artifact hashes:")
    for label, paths in artifacts.items():
        for path in paths:
            if path.exists():
                print(f"  {label} {path.name} {sha256(path)}")

    for desc, left, right, idx in comparisons:
        lp = artifacts[left][idx]
        rp = artifacts[right][idx]
        if lp.exists() and rp.exists():
            lhash = sha256(lp)
            rhash = sha256(rp)
            print(f"compare {desc}: {left}={lhash} {right}={rhash}")
            if lhash != rhash:
                problems.append(f"{desc} mismatch: {left} vs {right}")

    if candidate_linear_runtime is not None:
        linear = artifacts["candidate_linear_loads_baseline"]
        print("linear candidate baseline-save final RAM:", sha256(linear[1]))
        print("linear candidate baseline-save final PNG:", sha256(linear[2]))
    if not args.skip_env_quality_override:
        env_override = artifacts["candidate_config0_env_quality1_loads_baseline"]
        print("config0+SDL_RENDER_SCALE_QUALITY=1 final RAM:", sha256(env_override[1]))
        print("config0+SDL_RENDER_SCALE_QUALITY=1 final PNG:", sha256(env_override[2]))

    summary = out_dir / "summary.txt"
    lines = ["Sonic 1 cross-state validation", f"baseline={baseline_exe}", f"candidate={candidate_exe}", f"rom={rom}", ""]
    for label, proc, log, _ in runs:
        lines.append(f"{label}: exit={proc.returncode} log={log}")
    lines.append("")
    for label, paths in artifacts.items():
        for path in paths:
            if path.exists():
                lines.append(f"{label} {path.name} {sha256(path)}")
    if problems:
        lines.append("")
        lines.append("FAILURES:")
        lines.extend(problems)
    else:
        lines.append("")
        lines.append("PASS")
    summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"summary={summary}")

    if problems:
        for p in problems:
            print("ERROR:", p, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

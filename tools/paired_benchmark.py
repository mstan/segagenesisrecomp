#!/usr/bin/env python3
"""Run balanced, CPU-pinned Genesis benchmark pairs.

Both binaries are run with the same ROM, frame count, and logical-CPU
affinity. Pair order alternates B/A then A/B to reduce time-order bias.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path


BENCHMARK_PREFIX = "GENESISRECOMP_BENCHMARK "


def set_process_affinity(pid: int, cpu: int) -> None:
    if sys.platform == "win32":
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = [
            ctypes.c_uint32,
            ctypes.c_int,
            ctypes.c_uint32,
        ]
        kernel32.OpenProcess.restype = ctypes.c_void_p
        kernel32.SetProcessAffinityMask.argtypes = [
            ctypes.c_void_p,
            ctypes.c_size_t,
        ]
        kernel32.SetProcessAffinityMask.restype = ctypes.c_int
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]

        process_set_information = 0x0200
        process_query_information = 0x0400
        handle = kernel32.OpenProcess(
            process_set_information | process_query_information, 0, pid
        )
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            if not kernel32.SetProcessAffinityMask(handle, 1 << cpu):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            kernel32.CloseHandle(handle)
    elif hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(pid, {cpu})
    else:
        raise RuntimeError(f"CPU affinity is unsupported on {sys.platform}")


def parse_benchmark(stdout: str, stderr: str) -> dict:
    for line in (stdout + "\n" + stderr).splitlines():
        if line.startswith(BENCHMARK_PREFIX):
            return json.loads(line[len(BENCHMARK_PREFIX) :])
    raise RuntimeError("runner produced no GENESISRECOMP_BENCHMARK record")


def run_once(
    label: str,
    exe: Path,
    rom: Path,
    input_script: Path | None,
    frames: int,
    cpu: int,
    timeout: float,
) -> dict:
    command = [str(exe), "--benchmark", str(frames), str(rom)]
    if input_script is not None:
        command += ["--input-script", str(input_script)]
    proc = subprocess.Popen(
        command,
        cwd=exe.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        set_process_affinity(proc.pid, cpu)
        stdout, stderr = proc.communicate(timeout=timeout)
    except BaseException:
        proc.kill()
        proc.communicate()
        raise

    if proc.returncode != 0:
        tail = "\n".join((stdout + "\n" + stderr).splitlines()[-30:])
        raise RuntimeError(
            f"{label} exited with code {proc.returncode}\n{tail}"
        )

    result = parse_benchmark(stdout, stderr)
    result["label"] = label
    return result


def coefficient_of_variation(values: list[float]) -> float:
    mean = statistics.fmean(values)
    return 0.0 if not mean else statistics.pstdev(values) / mean * 100.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument(
        "--input",
        type=Path,
        help="optional input script applied identically to both binaries",
    )
    parser.add_argument("--frames", type=int, default=6000)
    parser.add_argument("--warmup-frames", type=int, default=600)
    parser.add_argument("--pairs", type=int, default=2)
    parser.add_argument("--cpu", type=int, required=True)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument(
        "--max-pair-spread",
        type=float,
        default=3.0,
        help="maximum difference between paired deltas, in percentage points",
    )
    args = parser.parse_args()

    logical_cpus = os.cpu_count() or 1
    if not 0 <= args.cpu < logical_cpus:
        parser.error(f"--cpu must be in 0..{logical_cpus - 1}")
    if args.frames <= 0 or args.warmup_frames <= 0:
        parser.error("--frames and --warmup-frames must be positive")
    if args.pairs < 2:
        parser.error("--pairs must be at least 2 for a meaningful spread gate")

    baseline = args.baseline.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    rom = args.rom.resolve(strict=True)
    input_script = args.input.resolve(strict=True) if args.input else None

    print(
        f"[paired_benchmark] cpu={args.cpu} frames={args.frames} "
        f"pairs={args.pairs}"
    )
    print("[paired_benchmark] warming baseline and candidate")
    warm_baseline = run_once(
        "baseline-warmup",
        baseline,
        rom,
        input_script,
        args.warmup_frames,
        args.cpu,
        args.timeout,
    )
    warm_candidate = run_once(
        "candidate-warmup",
        candidate,
        rom,
        input_script,
        args.warmup_frames,
        args.cpu,
        args.timeout,
    )
    if warm_baseline["game"] != warm_candidate["game"]:
        raise RuntimeError(
            "baseline and candidate report different games: "
            f"{warm_baseline['game']} vs {warm_candidate['game']}"
        )

    pair_results = []
    baseline_fps = []
    candidate_fps = []
    for pair_index in range(args.pairs):
        order = (
            [("baseline", baseline), ("candidate", candidate)]
            if pair_index % 2 == 0
            else [("candidate", candidate), ("baseline", baseline)]
        )
        results = {}
        print(
            f"[paired_benchmark] pair {pair_index + 1}: "
            + " then ".join(label for label, _ in order)
        )
        for label, exe in order:
            result = run_once(
                label,
                exe,
                rom,
                input_script,
                args.frames,
                args.cpu,
                args.timeout,
            )
            if result["game"] != warm_baseline["game"]:
                raise RuntimeError(
                    f"{label} reported unexpected game {result['game']}"
                )
            results[label] = result
            print(
                f"  {label}: {result['fps']:.3f} FPS "
                f"({result['seconds']:.6f}s)"
            )

        base_fps = float(results["baseline"]["fps"])
        cand_fps = float(results["candidate"]["fps"])
        delta = (cand_fps / base_fps - 1.0) * 100.0
        baseline_fps.append(base_fps)
        candidate_fps.append(cand_fps)
        pair_results.append(
            {
                "pair": pair_index + 1,
                "order": [label for label, _ in order],
                "baseline_fps": base_fps,
                "candidate_fps": cand_fps,
                "delta_pct": delta,
            }
        )
        print(f"  paired delta: {delta:+.3f}%")

    deltas = [pair["delta_pct"] for pair in pair_results]
    spread = max(deltas) - min(deltas)
    summary = {
        "game": warm_baseline["game"],
        "cpu": args.cpu,
        "frames": args.frames,
        "input_script": str(input_script) if input_script else None,
        "pairs": pair_results,
        "median_delta_pct": statistics.median(deltas),
        "pair_spread_pct_points": spread,
        "baseline_cv_pct": coefficient_of_variation(baseline_fps),
        "candidate_cv_pct": coefficient_of_variation(candidate_fps),
        "stable": spread <= args.max_pair_spread,
    }
    print("GENESISRECOMP_PAIRED " + json.dumps(summary, separators=(",", ":")))
    if not summary["stable"]:
        print(
            f"[paired_benchmark] unstable: pair spread {spread:.3f} exceeds "
            f"{args.max_pair_spread:.3f} percentage points",
            file=sys.stderr,
        )
        return 4
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"[paired_benchmark] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)

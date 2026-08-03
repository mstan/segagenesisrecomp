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
HASH_FIELDS = ("state_fnv1a64", "audio_state_fnv1a64")


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


def set_process_priority(pid: int, priority: str) -> None:
    if priority == "normal":
        return
    if sys.platform != "win32":
        if hasattr(os, "setpriority") and hasattr(os, "PRIO_PROCESS"):
            nice_value = -5 if priority == "above-normal" else -10
            os.setpriority(os.PRIO_PROCESS, pid, nice_value)
            return
        raise RuntimeError(f"--priority {priority} is unsupported on this host")

    classes = {
        "above-normal": 0x00008000,
        "high": 0x00000080,
    }
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = [
        ctypes.c_uint32,
        ctypes.c_int,
        ctypes.c_uint32,
    ]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel32.SetPriorityClass.restype = ctypes.c_int
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]

    process_set_information = 0x0200
    process_query_information = 0x0400
    handle = kernel32.OpenProcess(
        process_set_information | process_query_information, 0, pid
    )
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if not kernel32.SetPriorityClass(handle, classes[priority]):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel32.CloseHandle(handle)


def parse_benchmark(stdout: str, stderr: str) -> dict:
    for line in (stdout + "\n" + stderr).splitlines():
        if line.startswith(BENCHMARK_PREFIX):
            return json.loads(line[len(BENCHMARK_PREFIX) :])
    raise RuntimeError("runner produced no GENESISRECOMP_BENCHMARK record")


def parse_env_overrides(values: list[str]) -> dict[str, str]:
    overrides: dict[str, str] = {}
    for value in values:
        name, separator, setting = value.partition("=")
        if not separator or not name:
            raise ValueError(
                f"environment override must be NAME=VALUE, got {value!r}"
            )
        overrides[name] = setting
    return overrides


def run_once(
    label: str,
    exe: Path,
    rom: Path,
    input_script: Path | None,
    frames: int,
    cpu: int,
    priority: str,
    timeout: float,
    env_overrides: dict[str, str],
) -> dict:
    command = [str(exe), "--benchmark", str(frames), str(rom)]
    if input_script is not None:
        command += ["--input-script", str(input_script)]
    child_env = os.environ.copy()
    child_env.update(env_overrides)
    proc = subprocess.Popen(
        command,
        cwd=exe.parent,
        env=child_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        set_process_affinity(proc.pid, cpu)
        set_process_priority(proc.pid, priority)
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


def verify_hashes(
    baseline: dict,
    candidate: dict,
    context: str,
    allow_missing: bool,
) -> None:
    for field in HASH_FIELDS:
        base_hash = baseline.get(field)
        cand_hash = candidate.get(field)
        if base_hash is None or cand_hash is None:
            if allow_missing:
                continue
            raise RuntimeError(
                f"{context}: missing {field}; rebuild both runners with "
                "benchmark hash support or pass --allow-missing-hashes"
            )
        if base_hash != cand_hash:
            raise RuntimeError(
                f"{context}: {field} mismatch: baseline={base_hash}, "
                f"candidate={cand_hash}"
            )


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
    parser.add_argument(
        "--metric",
        choices=("fps", "cpu_fps", "cycles_per_frame"),
        default="fps",
        help="throughput metric used for paired deltas and variance",
    )
    parser.add_argument(
        "--priority",
        choices=("normal", "above-normal", "high"),
        default="normal",
        help="priority class/nice level for each pinned benchmark child",
    )
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument(
        "--baseline-env",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="environment override for baseline runs; may be repeated",
    )
    parser.add_argument(
        "--candidate-env",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="environment override for candidate runs; may be repeated",
    )
    parser.add_argument(
        "--max-pair-spread",
        type=float,
        default=3.0,
        help="maximum difference between paired deltas, in percentage points",
    )
    parser.add_argument(
        "--allow-missing-hashes",
        action="store_true",
        help="permit historical runners that predate benchmark state hashes",
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
    try:
        baseline_env = parse_env_overrides(args.baseline_env)
        candidate_env = parse_env_overrides(args.candidate_env)
    except ValueError as exc:
        parser.error(str(exc))

    print(
        f"[paired_benchmark] cpu={args.cpu} frames={args.frames} "
        f"pairs={args.pairs} priority={args.priority} metric={args.metric}"
    )
    print("[paired_benchmark] warming baseline and candidate")
    warm_baseline = run_once(
        "baseline-warmup",
        baseline,
        rom,
        input_script,
        args.warmup_frames,
        args.cpu,
        args.priority,
        args.timeout,
        baseline_env,
    )
    warm_candidate = run_once(
        "candidate-warmup",
        candidate,
        rom,
        input_script,
        args.warmup_frames,
        args.cpu,
        args.priority,
        args.timeout,
        candidate_env,
    )
    if warm_baseline["game"] != warm_candidate["game"]:
        raise RuntimeError(
            "baseline and candidate report different games: "
            f"{warm_baseline['game']} vs {warm_candidate['game']}"
        )
    verify_hashes(
        warm_baseline,
        warm_candidate,
        "warmup",
        args.allow_missing_hashes,
    )

    pair_results = []
    baseline_values = []
    candidate_values = []
    for pair_index in range(args.pairs):
        order = (
            [
                ("baseline", baseline, baseline_env),
                ("candidate", candidate, candidate_env),
            ]
            if pair_index % 2 == 0
            else [
                ("candidate", candidate, candidate_env),
                ("baseline", baseline, baseline_env),
            ]
        )
        results = {}
        print(
            f"[paired_benchmark] pair {pair_index + 1}: "
            + " then ".join(label for label, _, _ in order)
        )
        for label, exe, env_overrides in order:
            result = run_once(
                label,
                exe,
                rom,
                input_script,
                args.frames,
                args.cpu,
                args.priority,
                args.timeout,
                env_overrides,
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
        if args.metric not in results["baseline"] or args.metric not in results["candidate"]:
            raise RuntimeError(
                f"selected metric {args.metric} is missing; rebuild both runners "
                "with benchmark CPU accounting support"
            )
        base_value = float(results["baseline"][args.metric])
        cand_value = float(results["candidate"][args.metric])
        verify_hashes(
            results["baseline"],
            results["candidate"],
            f"pair {pair_index + 1}",
            args.allow_missing_hashes,
        )
        delta = (
            (base_value / cand_value - 1.0) * 100.0
            if args.metric == "cycles_per_frame"
            else (cand_value / base_value - 1.0) * 100.0
        )
        baseline_values.append(base_value)
        candidate_values.append(cand_value)
        pair_results.append(
            {
                "pair": pair_index + 1,
                "order": [label for label, _, _ in order],
                "baseline_fps": base_fps,
                "candidate_fps": cand_fps,
                "baseline_metric": base_value,
                "candidate_metric": cand_value,
                "delta_pct": delta,
            }
        )
        print(f"  paired delta: {delta:+.3f}%")

    deltas = [pair["delta_pct"] for pair in pair_results]
    spread = max(deltas) - min(deltas)
    summary = {
        "game": warm_baseline["game"],
        "cpu": args.cpu,
        "priority": args.priority,
        "metric": args.metric,
        "frames": args.frames,
        "input_script": str(input_script) if input_script else None,
        "pairs": pair_results,
        "median_delta_pct": statistics.median(deltas),
        "pair_spread_pct_points": spread,
        "baseline_cv_pct": coefficient_of_variation(baseline_values),
        "candidate_cv_pct": coefficient_of_variation(candidate_values),
        "state_fnv1a64": pair_results and results["baseline"].get("state_fnv1a64"),
        "audio_state_fnv1a64": (
            pair_results and results["baseline"].get("audio_state_fnv1a64")
        ),
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

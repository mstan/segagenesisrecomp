#!/usr/bin/env python3
"""netplay_regression.py - 7-target deterministic fingerprint gate.

Builds every Genesis game target against a given engine checkout, runs a
fixed headless attract workload on each, and emits one JSON fingerprint that
later runs are compared against.  Written for the netplay/rollback work
(beads-3vb.9.1): every refactor of the frame loop must leave all seven games
bit-identical unless a change is root-caused and recorded (PRINCIPLES #23).

Targets:  s1 s2 s3 s3k sk rka puyo  (5 build trees: S1, S2, S3-family, RKA, Puyo)

Fingerprint per target (all from existing runner instrumentation):
  * GENESISRECOMP_BENCHMARK state_fnv1a64 + audio_state_fnv1a64 at run end
    (cosim_state_hash: 68K regs, timing, WRAM, Z80, Z80 RAM, bus, VDP, FM, PSG,
    event queue).
  * [FBHASH] framebuffer FNV every --hash-every frames (default 1 = every frame).
  * [MODEHASH] state-anchored framebuffer hash on each Game_Mode transition.
  * sha256 of the pre-resample mixer output (GENESIS_AUDIO_PREDRC fm + psg).
  * dispatch_misses.toml contents (must have an empty functions.extra list).

Each run executes in a fresh copy of the Release directory, so persisted
host state (.srm, settings.ini, rom-*.cfg, logs) can never leak between runs.
No debug.ini is copied, so no TCP port is opened; a distinct --port is still
passed per target.

Usage:
  netplay_regression.py build   --engine ROOT --out DIR [--targets ...]
  netplay_regression.py run     --out DIR --json FILE [--frames N] [--targets ...]
  netplay_regression.py compare A.json B.json
  netplay_regression.py all     --engine ROOT --out DIR --json FILE   (build + run)
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

IS_WINDOWS = os.name == "nt"

# Workspace holding the game repos side by side; override with
# GENESIS_WORKSPACE when the checkout lives elsewhere.  Windows keeps the
# historical F:/ default; elsewhere the default is the directory that holds
# this engine checkout (game repos sit beside it).
_DEFAULT_WS = (r"F:/Projects/segagenesisrecomp" if IS_WINDOWS
               else str(Path(__file__).resolve().parents[2]))
WORKSPACE = Path(os.environ.get("GENESIS_WORKSPACE", _DEFAULT_WS))
_VS_CMAKE = Path(r"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/"
                 r"Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")
# GENESIS_CMAKE / GENESIS_CMAKE_GENERATOR override the platform default
# (Windows: VS2022 BuildTools cmake + "Visual Studio 17 2022"; elsewhere: the
# PATH cmake + Ninja, single-config Release).
CMAKE = Path(os.environ.get("GENESIS_CMAKE") or
             (str(_VS_CMAKE) if (IS_WINDOWS and _VS_CMAKE.exists())
              else (shutil.which("cmake") or "cmake")))
GENERATOR = os.environ.get("GENESIS_CMAKE_GENERATOR",
                           "Visual Studio 17 2022" if IS_WINDOWS else "Ninja")
# Visual Studio (and Ninja Multi-Config / Xcode) put binaries in <bdir>/Release;
# single-config generators put them in <bdir> and need CMAKE_BUILD_TYPE.
MULTI_CONFIG = GENERATOR.startswith(("Visual Studio", "Xcode")) or "Multi-Config" in GENERATOR
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""

# Build trees.  "source" is the game repo; puyo has no GENESIS_RECOMP_ROOT
# override (it only honours an `engine-local` link next to its CMakeLists), so
# it is configured from a scratch source dir holding a byte-identical copy of
# its CMakeLists.txt plus an engine-local junction -> the engine under test.
REPOS = {
    "S1":   {"source": WORKSPACE / "SonicTheHedgehogRecomp",       "override": True},
    "S2":   {"source": WORKSPACE / "SonicTheHedgehog2Recomp",      "override": True},
    "S3F":  {"source": WORKSPACE / "Sonic3AndKnucklesRecomp",      "override": True},
    "RKA":  {"source": WORKSPACE / "RocketKnightAdventuresRecomp", "override": True},
    "PUYO": {"source": WORKSPACE / "PuyoPuyoRecomp",               "override": False},
}

TARGETS = {
    #  name    repo    cmake target               ROM           port
    "s1":   ("S1",   "SonicTheHedgehogRecomp",  "sonic.bin",   4520),
    "s2":   ("S2",   "SonicTheHedgehog2Recomp", "sonic2.bin",  4521),
    "s3":   ("S3F",  "Sonic3Recomp",            "sonic3.bin",  4522),
    "s3k":  ("S3F",  "Sonic3KRecomp",           "sonic3k.bin", 4523),
    "sk":   ("S3F",  "SonicAndKnucklesRecomp",  "sandk.bin",   4524),
    "rka":  ("RKA",  "RKARecomp",               "rka.bin",     4525),
    "puyo": ("PUYO", "PuyoRecomp",              "puyo.bin",    4526),
}

# Where each ROM lives when the build does not stage it next to the exe
# ("{engine}" = the engine checkout under test).
ROM_SOURCES = {
    "s1":   "{engine}/sonicthehedgehog/sonic.bin",
    "s2":   str(WORKSPACE / "SonicTheHedgehog2Recomp/game/sonic2.bin"),
    "s3":   "{engine}/sonic3/sonic3.bin",
    "s3k":  "{engine}/sonic3k/sonic3k.bin",
    "sk":   "{engine}/sandk/sandk.bin",
    "rka":  "{engine}/rka/rka.bin",
    "puyo": "{engine}/puyo/puyo.bin",
}

# Files a runner may leave next to its exe; never copied into a run dir.
SKIP_SUFFIXES = {".pdb", ".ilk", ".map", ".lib", ".exp", ".log", ".srm",
                 ".toml", ".cfg", ".ini", ".txt", ".wav", ".png", ".s16"}
SKIP_NAMES = {"GenesisRecomp.exe", "GenesisRecomp", "genesis-recompiler"}


def log(msg: str) -> None:
    print(msg, flush=True)


# --------------------------------------------------------------------------
# build
# --------------------------------------------------------------------------

def _junction(link: Path, target: Path) -> None:
    """Directory link: an NTFS junction on Windows, a symlink elsewhere."""
    if link.exists() or os.path.islink(link):
        try:
            cur = Path(os.path.realpath(link))
        except OSError:
            cur = None
        if cur and cur.resolve() == target.resolve():
            return
        if IS_WINDOWS:
            subprocess.run(["cmd", "/c", "rmdir", str(link)], check=True)
        else:
            os.unlink(link)
    if IS_WINDOWS:
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)],
                       check=True, stdout=subprocess.DEVNULL)
    else:
        os.symlink(target, link, target_is_directory=True)


def _puyo_source(out: Path, engine: Path) -> Path:
    src = out / "puyo-src"
    src.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(REPOS["PUYO"]["source"] / "CMakeLists.txt", src / "CMakeLists.txt")
    _junction(src / "engine-local", engine)
    return src


def _puyo_compat(out: Path) -> Path:
    f = out / "puyo-compat.cmake"
    f.write_text(
        "# netplay_regression.py: build-configuration-only Puyo link fix.\n"
        "function(_npreg_puyo_add_cosim_state)\n"
        "  get_target_property(_s PuyoRecomp SOURCES)\n"
        "  if(NOT \"${RUNNER_ROOT}/cosim_state.c\" IN_LIST _s)\n"
        "    target_sources(PuyoRecomp PRIVATE \"${RUNNER_ROOT}/cosim_state.c\")\n"
        "  endif()\n"
        "endfunction()\n"
        "cmake_language(DEFER CALL _npreg_puyo_add_cosim_state)\n")
    return f


def build_repo(repo: str, engine: Path, out: Path, jobs: int) -> dict:
    info = REPOS[repo]
    if not (info["source"] / "CMakeLists.txt").exists():
        # A game repo (or its ROM) that is not present locally is recorded as
        # unavailable, never silently dropped: the gate report names it.
        log(f"[build] {repo}: UNAVAILABLE (no checkout at {info['source']})")
        return {"repo": repo, "ok": False, "unavailable": True,
                "log": "", "inject": ""}
    bdir = out / repo
    bdir.mkdir(parents=True, exist_ok=True)
    source = info["source"] if info["override"] else _puyo_source(out, engine)
    cfg = [str(CMAKE), "-S", str(source), "-B", str(bdir), "-G", GENERATOR]
    if GENERATOR.startswith("Visual Studio"):
        cfg += ["-A", "x64"]
    if not MULTI_CONFIG:
        cfg.append("-DCMAKE_BUILD_TYPE=Release")
    if info["override"]:
        cfg.append(f"-DGENESIS_RECOMP_ROOT={engine.as_posix()}")
    inject = ""
    if repo == "PUYO":
        # Puyo's CMakeLists predates cosim_state.c becoming a hard runner
        # dependency (main.c's --benchmark fingerprint) and cannot link
        # against any current engine without it.  Add it from THIS build
        # configuration only (project-scoped include + deferred call), never
        # by editing the Puyo repo; identical for baseline and candidate.
        inject = str(_puyo_compat(out))
        cfg.append(f"-DCMAKE_PROJECT_PuyoPuyoRecomp_INCLUDE={Path(inject).as_posix()}")
    blog = out / f"{repo}.build.log"
    t0 = time.time()
    with open(blog, "w", encoding="utf-8", errors="replace") as fh:
        r = subprocess.run(cfg, stdout=fh, stderr=subprocess.STDOUT)
        if r.returncode == 0:
            r = subprocess.run([str(CMAKE), "--build", str(bdir), "--config", "Release",
                                "--parallel", str(jobs)],
                               stdout=fh, stderr=subprocess.STDOUT)
    ok = r.returncode == 0
    log(f"[build] {repo}: {'OK' if ok else 'FAILED'} ({time.time()-t0:.0f}s) log={blog}")
    return {"repo": repo, "ok": ok, "log": str(blog), "inject": inject or ""}


def cmd_build(a) -> int:
    engine = Path(a.engine).resolve()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    repos = sorted({TARGETS[t][0] for t in a.targets})
    res = []
    with cf.ThreadPoolExecutor(max_workers=a.concurrency) as ex:
        futs = [ex.submit(build_repo, r, engine, out, a.jobs) for r in repos]
        for f in futs:
            res.append(f.result())
    prev = {}
    if (out / "build_meta.json").exists():
        prev = {b["repo"]: b for b in json.loads((out / "build_meta.json").read_text())
                .get("builds", [])}
    for r in res:
        prev[r["repo"]] = r
    meta = {"engine": str(engine), "engine_head": _git_head(engine),
            "engine_dirty": _git_dirty(engine), "engine_submodules": _git_subs(engine),
            "builds": [prev[k] for k in sorted(prev)]}
    (out / "build_meta.json").write_text(json.dumps(meta, indent=2))
    return 0 if all(r["ok"] or r.get("unavailable") for r in res) else 1


def _git_head(p: Path) -> str:
    try:
        return subprocess.run(["git", "-C", str(p), "rev-parse", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
    except OSError:
        return ""


def _git_subs(p: Path) -> list:
    try:
        s = subprocess.run(["git", "-C", str(p), "submodule", "status"],
                           capture_output=True, text=True).stdout
        return [ln.strip() for ln in s.splitlines() if ln.strip()]
    except OSError:
        return []


def _git_dirty(p: Path) -> list:
    try:
        s = subprocess.run(["git", "-C", str(p), "status", "--short"],
                           capture_output=True, text=True).stdout
        return [ln for ln in s.splitlines() if ln.strip()]
    except OSError:
        return []


# --------------------------------------------------------------------------
# run
# --------------------------------------------------------------------------

FB_RE = re.compile(r"^\[FBHASH\] frame=(\d+) w=(\d+) h=(\d+) hash=0x([0-9A-Fa-f]+)")
MODE_RE = re.compile(r"^\[MODEHASH\] seq=(\d+) mode=0x([0-9A-Fa-f]+) frame=(\d+) "
                     r"w=(\d+) h=(\d+) hash=0x([0-9A-Fa-f]+)")


def _sha256(p: Path) -> str:
    if not p.exists():
        return "missing"
    h = hashlib.sha256()
    with open(p, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _release_dir(out: Path, repo: str) -> Path:
    return out / repo / "Release" if MULTI_CONFIG else out / repo


def _prepare_run_dir(release: Path, run_dir: Path, exe_name: str = "",
                     rom: str = "") -> None:
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True)
    if not MULTI_CONFIG:
        # Single-config trees mix binaries with build files (build.ninja,
        # generated/, CMakeFiles/...): stage an explicit allow-list instead.
        for e in release.iterdir():
            keep = (e.name in (exe_name, rom, "annotations_from_disasm.csv")
                    or e.suffix.lower() in (".so", ".dylib", ".dll"))
            if e.is_dir() and e.name == "assets":
                shutil.copytree(e, run_dir / e.name)
            elif e.is_file() and keep:
                shutil.copy2(e, run_dir / e.name)
        return
    for e in release.iterdir():
        if e.name in SKIP_NAMES:
            continue
        if e.is_dir():
            if e.name == "assets":
                shutil.copytree(e, run_dir / e.name)
            continue
        if e.suffix.lower() in SKIP_SUFFIXES and e.name != "annotations_from_disasm.csv":
            continue
        shutil.copy2(e, run_dir / e.name)
    csv = release / "annotations_from_disasm.csv"
    if csv.exists():
        shutil.copy2(csv, run_dir / csv.name)


# Workloads.  "attract" = no input.  The scripted scenarios drive the same
# fixed .input timeline into every game (runner/input_script.h):
#   savestate - attract, plus SAVE_STATE / LOAD_STATE twice mid-run: covers the
#               quickstate path and glue_restart_game_fiber (fiber reset).
#   gameplay  - taps START through title/menus, then holds RIGHT and jumps,
#               with a save/load pair inside gameplay.
def scenario_script(scenario: str) -> str:
    lines = ["# netplay_regression.py scenario: " + scenario]
    if scenario == "savestate":
        lines += ["WAIT 2400", "SAVE_STATE np_state.bin", "WAIT 600",
                  "LOAD_STATE np_state.bin", "WAIT 900", "LOAD_STATE np_state.bin"]
    elif scenario == "gameplay":
        lines += ["WAIT 240"]
        for _ in range(12):
            lines += ["PRESS START 4", "WAIT 150"]
        lines += ["HOLD RIGHT"]
        for i in range(40):
            lines += ["PRESS C 12", "WAIT 90"]
            if i == 10:
                lines += ["SAVE_STATE np_state.bin"]
            if i == 20:
                lines += ["LOAD_STATE np_state.bin"]
    else:
        raise ValueError(scenario)
    return "\n".join(lines) + "\n"


def run_target(name: str, out: Path, frames: int, every: int, run_tag: str,
               timeout: int, engine: str, extra_env: dict,
               scenario: str = "attract") -> dict:
    repo, exe_name, rom, port = TARGETS[name]
    release = _release_dir(out, repo)
    exe = release / f"{exe_name}{EXE_SUFFIX}"
    res: dict = {"target": name, "exe": str(exe)}
    if not exe.exists():
        res["error"] = "exe missing"
        return res
    res["exe_sha256"] = _sha256(exe)
    run_dir = out / "runs" / run_tag / name
    _prepare_run_dir(release, run_dir, exe.name, rom)
    if not (run_dir / rom).exists() and engine:
        src = Path(ROM_SOURCES[name].format(engine=engine))
        if src.exists():
            shutil.copy2(src, run_dir / rom)
    res["rom_sha256"] = _sha256(run_dir / rom)
    if not (run_dir / rom).exists():
        res["error"] = f"rom {rom} missing in run dir"
        return res
    # Hermetic environment: no inherited runtime knob may perturb the run.
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(("GENESIS_", "RNET_", "SNES_"))}
    env.update(extra_env)
    env["GENESIS_AUDIO_PREDRC"] = str(run_dir / "predrc")
    env["GENESIS_NO_LAUNCHER"] = "1"
    res["extra_env"] = dict(sorted(extra_env.items()))
    if not IS_WINDOWS:
        env.setdefault("SDL_VIDEODRIVER", "dummy")
        env.setdefault("SDL_AUDIODRIVER", "dummy")
    args = [str(run_dir / f"{exe_name}{EXE_SUFFIX}"), rom, "--benchmark", str(frames),
            "--hash-frames", str(every), "--hash-on-mode", "--port", str(port)]
    res["scenario"] = scenario
    if scenario != "attract":
        (run_dir / "scenario.input").write_text(scenario_script(scenario))
        args += ["--input-script", "scenario.input"]
    t0 = time.time()
    with open(run_dir / "stdout.log", "wb") as so, open(run_dir / "stderr.log", "wb") as se:
        try:
            p = subprocess.run(args, cwd=run_dir, env=env, stdout=so, stderr=se,
                               timeout=timeout)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    res["seconds"] = round(time.time() - t0, 2)
    res["returncode"] = rc
    stdout = (run_dir / "stdout.log").read_text(errors="replace")
    stderr = (run_dir / "stderr.log").read_text(errors="replace")
    bench = None
    for ln in stdout.splitlines():
        if ln.startswith("GENESISRECOMP_BENCHMARK "):
            bench = json.loads(ln[len("GENESISRECOMP_BENCHMARK "):])
    if bench:
        res["frames"] = bench.get("frames")
        res["state_fnv1a64"] = bench.get("state_fnv1a64")
        res["audio_state_fnv1a64"] = bench.get("audio_state_fnv1a64")
        res["fps"] = bench.get("fps")
    else:
        res["error"] = "no GENESISRECOMP_BENCHMARK line"
    fb, modes = [], []
    for ln in stderr.splitlines():
        m = FB_RE.match(ln)
        if m:
            fb.append([int(m[1]), int(m[2]), int(m[3]), m[4].upper()])
            continue
        m = MODE_RE.match(ln)
        if m:
            modes.append([int(m[1]), m[2].upper(), int(m[3]), int(m[4]), int(m[5]),
                          m[6].upper()])
    res["fbhash_count"] = len(fb)
    res["fbhash_digest"] = hashlib.sha256(json.dumps(fb).encode()).hexdigest()
    res["fbhash"] = fb
    res["modehash"] = modes
    res["predrc_fm_sha256"] = _sha256(run_dir / "predrc.fm.s16")
    res["predrc_psg_sha256"] = _sha256(run_dir / "predrc.psg.s16")
    res["ramdump_sha256"] = _sha256(run_dir / "ramdump_native.bin")
    dm = run_dir / "dispatch_misses.toml"
    dm_text = dm.read_text(errors="replace") if dm.exists() else ""
    res["dispatch_misses_raw"] = dm_text
    res["dispatch_misses_extra"] = _misses_extra(dm_text)
    res["interp_line"] = next((ln for ln in stderr.splitlines()
                               if ln.startswith("[INTERP]")), "")
    # Host lifecycle events the scripted scenarios must actually exercise.
    res["lifecycle"] = [ln.strip().replace(str(run_dir), "<run>").replace(run_dir.as_posix(), "<run>")
                        for ln in stderr.splitlines()
                        if ln.startswith(("[SAVE]", "[LOAD]", "[fiber] restarted",
                                          "[input_script]", "[host_state]", "[STATE]"))]
    log(f"[run] {run_tag}/{name}: rc={rc} frames={res.get('frames')} "
        f"state={res.get('state_fnv1a64')} audio={res.get('audio_state_fnv1a64')} "
        f"fb={len(fb)} modes={len(modes)} misses={len(res['dispatch_misses_extra'])} "
        f"({res['seconds']}s)")
    return res


def _misses_extra(text: str) -> list:
    """Entries of the [functions] extra array in dispatch_misses.toml."""
    m = re.search(r"extra\s*=\s*\[(.*?)\]", text, re.S)
    if not m:
        return []
    return [t.strip() for t in re.split(r"[,\n]", m[1])
            if t.strip() and not t.strip().startswith("#")]


def cmd_run(a) -> int:
    out = Path(a.out).resolve()
    tag = a.tag or time.strftime("run-%Y%m%d-%H%M%S")
    results = {}
    meta = {}
    bm = out / "build_meta.json"
    if bm.exists():
        meta = json.loads(bm.read_text())
    engine = meta.get("engine", "")
    # Never fingerprint stale executables: every build tree the requested
    # targets live in must have built successfully in its latest build.
    built = {b["repo"]: b.get("ok") for b in meta.get("builds", [])}
    unavailable = {b["repo"] for b in meta.get("builds", []) if b.get("unavailable")}
    skipped = [t for t in a.targets if TARGETS[t][0] in unavailable]
    for t in skipped:
        log(f"[run] {t}: UNAVAILABLE (repo {TARGETS[t][0]} not checked out) - not gated")
    a.targets = [t for t in a.targets if t not in skipped]
    stale = sorted({TARGETS[t][0] for t in a.targets if not built.get(TARGETS[t][0])})
    if stale:
        log(f"[run] REFUSED: last build of {', '.join(stale)} failed or is missing "
            f"(see {out / 'build_meta.json'})")
        return 2
    with cf.ThreadPoolExecutor(max_workers=a.concurrency) as ex:
        extra_env = dict(kv.split("=", 1) for kv in a.env)
        futs = {t: ex.submit(run_target, t, out, a.frames, a.hash_every, tag, a.timeout,
                             engine, extra_env, a.scenario)
                for t in a.targets}
        for t, f in futs.items():
            results[t] = f.result()
    doc = {"tool": "netplay_regression", "version": 1, "frames": a.frames,
           "unavailable": skipped,
           "scenario": a.scenario,
           "hash_every": a.hash_every, "run_tag": tag, "build": meta,
           "targets": results}
    Path(a.json).write_text(json.dumps(doc, indent=1))
    bad = [t for t, r in results.items()
           if r.get("error") or r.get("returncode") != 0 or r.get("dispatch_misses_extra")]
    for t in bad:
        r = results[t]
        log(f"[run] PROBLEM {t}: error={r.get('error')} rc={r.get('returncode')} "
            f"misses={r.get('dispatch_misses_extra')}")
    log(f"[run] wrote {a.json}")
    return 1 if bad else 0


# --------------------------------------------------------------------------
# compare
# --------------------------------------------------------------------------

SCALAR_FIELDS = ["extra_env", "rom_sha256", "frames", "returncode", "state_fnv1a64", "audio_state_fnv1a64",
                 "fbhash_count", "fbhash_digest", "predrc_fm_sha256",
                 "predrc_psg_sha256", "ramdump_sha256", "dispatch_misses_extra",
                 "interp_line", "lifecycle", "scenario"]


def compare_docs(a: dict, b: dict) -> list:
    diffs = []
    if (a.get("frames") != b.get("frames") or a.get("hash_every") != b.get("hash_every")
            or a.get("scenario", "attract") != b.get("scenario", "attract")):
        diffs.append(("*", "workload", f"{a.get('frames')}/{a.get('hash_every')} vs "
                                       f"{b.get('frames')}/{b.get('hash_every')}"))
    ta, tb = a["targets"], b["targets"]
    for t in sorted(set(ta) | set(tb)):
        ra, rb = ta.get(t), tb.get(t)
        if ra is None or rb is None:
            diffs.append((t, "presence", f"{ra is not None} vs {rb is not None}"))
            continue
        for f in SCALAR_FIELDS:
            if ra.get(f) != rb.get(f):
                diffs.append((t, f, f"{ra.get(f)} vs {rb.get(f)}"))
        fa, fb = ra.get("fbhash", []), rb.get("fbhash", [])
        for i in range(min(len(fa), len(fb))):
            if fa[i] != fb[i]:
                diffs.append((t, "first_fbhash_divergence", f"{fa[i]} vs {fb[i]}"))
                break
        ma, mb = ra.get("modehash", []), rb.get("modehash", [])
        if ma != mb:
            first = next((i for i in range(min(len(ma), len(mb))) if ma[i] != mb[i]),
                         min(len(ma), len(mb)))
            diffs.append((t, "modehash", f"len {len(ma)} vs {len(mb)}; first diff idx "
                          f"{first}: {ma[first] if first < len(ma) else None} vs "
                          f"{mb[first] if first < len(mb) else None}"))
    return diffs


def cmd_compare(a) -> int:
    da = json.loads(Path(a.a).read_text())
    db = json.loads(Path(a.b).read_text())
    diffs = compare_docs(da, db)
    for t in sorted(set(da["targets"]) | set(db["targets"])):
        mine = [d for d in diffs if d[0] == t]
        log(f"  {t:5s} {'IDENTICAL' if not mine else 'DIFFERS'}")
        for d in mine:
            log(f"        {d[1]}: {d[2]}")
    for d in diffs:
        if d[0] == "*":
            log(f"  workload mismatch: {d[2]}")
    for t in sorted(set(da.get("unavailable", [])) | set(db.get("unavailable", []))):
        log(f"  {t:5s} UNAVAILABLE (not built/run; not covered by this gate)")
    log("GATE: " + ("GREEN (all targets fingerprint-identical)" if not diffs else "RED"))
    return 0 if not diffs else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def targets_arg(p):
        p.add_argument("--targets", nargs="+", default=list(TARGETS),
                       choices=list(TARGETS))

    pb = sub.add_parser("build")
    pb.add_argument("--engine", required=True)
    pb.add_argument("--out", required=True)
    pb.add_argument("--jobs", type=int, default=os.cpu_count() or 8)
    pb.add_argument("--concurrency", type=int, default=2)
    targets_arg(pb)

    pr = sub.add_parser("run")
    pr.add_argument("--out", required=True)
    pr.add_argument("--json", required=True)
    pr.add_argument("--frames", type=int, default=18000)
    pr.add_argument("--hash-every", type=int, default=1)
    pr.add_argument("--tag", default="")
    pr.add_argument("--timeout", type=int, default=900)
    pr.add_argument("--concurrency", type=int, default=4)
    pr.add_argument("--scenario", default="attract",
                    choices=["attract", "savestate", "gameplay"])
    pr.add_argument("--env", nargs="*", default=[],
                    help="K=V runtime env for every target (negative controls)")
    targets_arg(pr)

    pc = sub.add_parser("compare")
    pc.add_argument("a")
    pc.add_argument("b")

    pa = sub.add_parser("all")
    pa.add_argument("--engine", required=True)
    pa.add_argument("--out", required=True)
    pa.add_argument("--json", required=True)
    pa.add_argument("--jobs", type=int, default=os.cpu_count() or 8)
    pa.add_argument("--concurrency", type=int, default=2)
    pa.add_argument("--frames", type=int, default=18000)
    pa.add_argument("--hash-every", type=int, default=1)
    pa.add_argument("--tag", default="")
    pa.add_argument("--timeout", type=int, default=900)
    pa.add_argument("--env", nargs="*", default=[])
    pa.add_argument("--scenario", default="attract",
                    choices=["attract", "savestate", "gameplay"])
    targets_arg(pa)

    a = ap.parse_args()
    if a.cmd == "build":
        return cmd_build(a)
    if a.cmd == "run":
        return cmd_run(a)
    if a.cmd == "compare":
        return cmd_compare(a)
    rc = cmd_build(a)
    if rc:
        return rc
    a.concurrency = 4
    return cmd_run(a)


if __name__ == "__main__":
    sys.exit(main())

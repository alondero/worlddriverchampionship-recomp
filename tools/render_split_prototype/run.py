"""THROWAWAY Windows/MinGW probe: relink an existing player without rebuilding it.

The reference build is read-only. All new objects, executable, settings and
evidence live under this worktree's ignored build/render-split-prototype.
GNU --wrap intercepts cross-object calls; runtime counters establish that the
sound and presentation boundaries were intercepted rather than optimized away.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess
import tempfile
from runtime import build_runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--mode", choices=["raw", "sound-gated", "camera-offset", "repeat", "render-only", "split60", "present120", "camera-snapshot"], default="sound-gated")
    parser.add_argument("--require-repeatable", action="store_true",
                        help="Fail unless repeat's normal and both extra draws have equal triangle headers and frozen camera/car checks pass")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--warp", default="1:0", help="Player race setup: track[:car[:mode[:laps]]]")
    parser.add_argument("--settle-frames", type=int, default=60, help="Eligible normal draws before six trials")
    args = parser.parse_args()
    if not 60 <= args.settle_frames <= 1800:
        parser.error("--settle-frames must be 60..1800; increase --timeout for long runs")
    if args.require_repeatable and args.mode not in ("repeat", "render-only", "present120", "camera-snapshot"):
        parser.error("--require-repeatable requires repeat, render-only, present120 or camera-snapshot")
    reference = args.reference_build.resolve()
    rom = args.rom.resolve()
    if hashlib.sha256(rom.read_bytes()).hexdigest() != "e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97":
        raise SystemExit("Expected the USA ROM recorded in rom-analysis.toml")
    here = Path(__file__).resolve().parent
    output = here.parents[1] / "build" / "render-split-prototype"
    output.mkdir(parents=True, exist_ok=True)
    cache = (reference / "CMakeCache.txt").read_text()
    source = Path(re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache, re.M)[1])
    compiler = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.*)$", cache, re.M)[1]
    ninja = (reference / "build.ninja").read_text()
    block = re.search(r"^build worlddriverchampionship_modern\.exe: .*?(?=\n\n)", ninja, re.M | re.S)[0]
    inputs = block.splitlines()[0].split(": ", 1)[1].split(" |", 1)[0].split(" ", 1)[1]
    if "$" in inputs:
        raise SystemExit("Prototype linker does not support escaped Ninja object paths")
    objects = shlex.split(inputs)
    def variable(name):
        found = re.search(r"^  " + name + r" = (.*)$", block, re.M)
        return shlex.split(found[1].replace("\\", "/")) if found else []
    probe = output / "probe.o"
    subprocess.run([compiler, "-std=c++20", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fno-strict-aliasing",
                    *(["-DWDC_PROTOTYPE_PRESENT"] if args.mode == "present120" else []),
                    "-I", str(source / "lib/N64ModernRuntime/N64Recomp/include"),
                    "-I", str(source / "lib/N64ModernRuntime/ultramodern/include"),
                    "-I", str(source / "src"),
                    "-c", str(here / "probe.cpp"), "-o", str(probe)], check=True)
    exe = output / "render_split_prototype.exe"
    runtime = [str(build_runtime(reference, output, compiler))] if args.mode == "present120" else []
    wrapped = ["func_8000C560", "func_8001F9CC", "func_800214C8", "osSpTaskStartGo_recomp",
               "func_80043274", "func_80042FA8", "func_8002A3A8",
               "func_8001FC64", "func_8001F7F8",
               "func_80024038", "func_80010A5C", "func_80010ECC", "func_80024FCC", "func_80025350",
               "func_800013DC", "osRecvMesg_recomp", "osViSwapBuffer_recomp",
               "wdc_framerate_preswap", "wdc_projection_normalize", "_ZN3wdc3rsp3runEPhPK6OSTaskPFvPvS1_jjES5_"]
    link = [*objects, str(probe), *runtime, *variable("LINK_PATH"), *variable("LINK_LIBRARIES"),
            *("-Wl,--wrap=" + name for name in wrapped), "-o", str(exe)]
    response = output / "link.rsp"
    response.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in link))
    subprocess.run([compiler, "@" + str(response)], cwd=reference, check=True)
    for dll in reference.glob("*.dll"):
        shutil.copy2(dll, output / dll.name)
    if (reference / "assets").exists():
        shutil.copytree(reference / "assets", output / "assets", dirs_exist_ok=True)
    # The player's config is relative to cwd/build; this is deliberately fresh.
    mode_dir = output / args.mode
    mode_dir.mkdir(exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="run-", dir=mode_dir))
    if runtime:
        shutil.copy2(output / "runtime-source.json", run_dir / "runtime-source.json")
    config = run_dir / "build" / "user-data"
    config.mkdir(parents=True, exist_ok=True)
    (config / "graphics.json").write_text(json.dumps({"resolution": 1, "native_60fps": False}))
    env = {key: value for key, value in os.environ.items() if not key.startswith("WDC_")}
    env.update(WDC_WARP=args.warp, WDC_RENDER_PROTOTYPE_MODE=args.mode,
               WDC_RENDER_PROTOTYPE_SETTLE=str(args.settle_frames))
    (run_dir / "setup.json").write_text(json.dumps({"warp": args.warp, "mode": args.mode,
                                                  "settle_frames": args.settle_frames}, indent=2))
    log = run_dir / "run.log"
    print(f"Running {args.mode} probe; local evidence: {log}", flush=True)
    with log.open("w") as stream:
        try:
            result = subprocess.run([str(exe), str(rom)], cwd=run_dir, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            raise SystemExit(f"Probe timed out; inspect {log}")
    rows = [json.loads(line.split("[render-prototype] ", 1)[1])
            for line in log.read_text(errors="replace").splitlines() if "[render-prototype] " in line]
    (run_dir / "report.json").write_text(json.dumps(rows, indent=2))
    geometry = [json.loads(line.split("[prototype-rdp] ", 1)[1])
                for line in log.read_text(errors="replace").splitlines() if "[prototype-rdp] " in line]
    (run_dir / "geometry.json").write_text(json.dumps(geometry, indent=2))
    snapshots = [json.loads(line.split("[prototype-snapshot] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-snapshot] " in line]
    (run_dir / "snapshots.json").write_text(json.dumps(snapshots, indent=2))
    projections = [json.loads(line.split("[prototype-projection] ", 1)[1])
                   for line in log.read_text(errors="replace").splitlines() if "[prototype-projection] " in line]
    (run_dir / "projections.json").write_text(json.dumps(projections, indent=2))
    presents = [json.loads(line.split("[prototype-present] ", 1)[1])
                for line in log.read_text(errors="replace").splitlines() if "[prototype-present] " in line]
    (run_dir / "presents.json").write_text(json.dumps(presents, indent=2))
    phases = [json.loads(line.split("[prototype-phase] ", 1)[1])
              for line in log.read_text(errors="replace").splitlines() if "[prototype-phase] " in line]
    (run_dir / "phases.json").write_text(json.dumps(phases, indent=2))
    if phases:
        for name in sorted({p['phase'] for p in phases}):
            values = [p['ms'] for p in phases if p['phase'] == name]
            print(f"{name}: mean {statistics.mean(values):.3f} ms, max {max(values):.3f} ms ({len(values)} calls)")
    for row in rows:
        print(f"trial {row['trial']}: extra draw {row['draw_ms']:.1f} ms; "
              f"samples {row['sample_before']} -> {row['sample_after']}; "
              f"sound buffer {row['sound_buffer_before']} -> {row['sound_buffer_after']}; "
              f"changed car words {len(row['car_word_changes'])}")
    if result.returncode or len(rows) != 6:
        raise SystemExit(f"Incomplete probe (exit {result.returncode}); inspect {log}")
    if any(row['sample_before'] != row['sample_after'] or
           row['divider_before'] != 1 or row['divider_after'] != 1 or
           row['ticks_before'] != 1562500 or row['ticks_after'] != 1562500 for row in rows):
        raise SystemExit("Simulation counter or stock sampling contract changed")
    repeats = 3 if args.mode == "present120" else (2 if args.mode in ("repeat", "render-only", "camera-snapshot") else 1)
    if args.mode != "raw" and any(row['skipped_publish'] != row['trial'] * repeats or
                                          row['skipped_consume'] != row['trial'] * repeats for row in rows):
        raise SystemExit("Sound wrappers did not intercept every extra draw")
    if args.mode in ("render-only", "present120", "camera-snapshot") and any(row['skipped_audio_build'] != row['trial'] * repeats or
            row['skipped_audio_retire'] != row['trial'] * repeats or not row['preserved_flare_history'] for row in rows):
        raise SystemExit("Render-only audio/history wrappers did not intercept")
    for row in rows:
        pair = sorted([g for g in geometry if g['frame'] == row['frame']], key=lambda g: g['draw'])
        if len(pair) != repeats + 1 or {g['draw'] for g in pair} != set(range(repeats + 1)) or any(not g['complete'] or not g['triangles'] for g in pair):
            raise SystemExit(f"Incomplete geometry evidence for frame {row['frame']}")
        print(f"frame {row['frame']}: normal/extra ordered triangle headers " +
              ("identical" if pair[0]['triangle_header_hash'] == pair[1]['triangle_header_hash'] else "DIFFERENT") +
              f"; triangle counts {[g['triangles'] for g in pair]}")
        if repeats >= 2:
            print("  consecutive extra draws: " +
                  ("identical" if pair[1]['triangle_header_hash'] == pair[2]['triangle_header_hash'] else "DIFFERENT"))
    if args.require_repeatable:
        frames = {row['frame'] for row in rows}
        for frame in frames:
            group = sorted((g for g in geometry if g['frame'] == frame), key=lambda g: g['draw'])
            if any(not g['same_headers_as_normal'] for g in group[1:]):
                raise SystemExit("Frozen-sample geometry differs (exact ordered triangle headers)")
            states = [s for s in snapshots if s['frame'] == frame]
            if len(states) != repeats or {s['draw'] for s in states} != set(range(1, repeats + 1)):
                raise SystemExit("Missing per-draw snapshot observations")
            if any(not s['context_unchanged'] or not s['camera_unchanged'] or
                   not s['car_nonlink_words_unchanged'] or not s['sample_unchanged'] for s in states):
                raise SystemExit("Frozen camera/car/context observation changed")
            if any(not s['car_count_unchanged'] or s['all_car_nonlink_changes'] for s in states):
                raise SystemExit("Frozen all-car non-link observation changed")
            if args.mode == "camera-snapshot" and any(s['camera_copies'] != 1 for s in states):
                raise SystemExit("Camera snapshot did not supply each single-view extra draw")
            if args.mode in ("render-only", "present120", "camera-snapshot") and any(
                    not s['preserved_flare_calls'] for s in states):
                raise SystemExit("Lens-flare preservation did not intercept each extra draw")
            if args.mode in ("render-only", "present120", "camera-snapshot") and any(
                    s['skipped_car_sound_calls'] != s['car_count'] for s in states):
                raise SystemExit("Single-view car sound gating did not intercept every car per extra draw")
            if args.mode in ("render-only", "present120", "camera-snapshot") and any(
                    change[0] % 0x1418 not in (0x360, 0x364) and not (
                        args.mode == "camera-snapshot" and change[0] in (0x18, 0x68, 0x6C)
                        and change[1] - change[2] == 128)
                    for s in states for change in s['view_word_changes']):
                raise SystemExit("Render-only draw changed per-view words outside flare render pointers")
            matrices = [p for p in projections if p['frame'] == frame]
            if len(matrices) != repeats + 1 or {p['draw'] for p in matrices} != set(range(repeats + 1)) or any(
                    p['matrix'] != matrices[0]['matrix'] for p in matrices):
                raise SystemExit("Missing or unequal single-view projection evidence")
        print("PASS: exact triangle headers, projection, camera snapshot, all-car non-link words and caller context repeat")
    if args.mode == "present120":
        if len(presents) != 24 or rows[-1]['skipped_vi_waits'] != 48:
            raise SystemExit("Missing direct presentation callbacks or swap-wait interception")
        intervals = [(b['us'] - a['us']) / 1000 for a, b in zip(presents, presents[1:])]
        rate = 1000 / statistics.mean(intervals)
        vi_ticks = presents[-1]['vi_ticks'] - presents[0]['vi_ticks']
        print(f"Renderer callbacks: {rate:.1f}/s; intervals {min(intervals):.2f}..{max(intervals):.2f} ms; {vi_ticks} VI ticks")
        if not 105 <= rate <= 135 or not 9 <= vi_ticks <= 14:
            raise SystemExit("120 Hz presentation / 60 Hz VI timing gate failed")


if __name__ == "__main__":
    main()

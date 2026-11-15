"""THROWAWAY Windows/MinGW probe: relink an existing player without rebuilding it.

The reference build is read-only. All new objects, executable, settings and
evidence live under this worktree's ignored build/render-split-prototype.
GNU --wrap intercepts cross-object calls; runtime counters establish that the
sound and presentation boundaries were intercepted rather than optimized away.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess
import tempfile
from runtime import build_runtime
from shadow import build_shadow_renderer
from rt64_profile import build_rt64_profile
from draw_boundary import build_draw_boundary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--mode", choices=["raw", "sound-gated", "camera-offset", "repeat", "render-only", "split60", "present120", "camera-snapshot", "shadow-snapshot", "shadow120", "pose-snapshot", "motion120", "endpoint120"], default="sound-gated")
    parser.add_argument("--require-repeatable", action="store_true",
                        help="Fail unless repeat's normal and both extra draws have equal triangle headers and frozen camera/car checks pass")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--warp", default="1:0", help="Player race setup: track[:car[:mode[:laps]]]")
    parser.add_argument("--settle-frames", type=int, default=60, help="Eligible normal draws before trials")
    parser.add_argument("--trials", type=int, default=6, help="Bounded trial count, 6..300")
    parser.add_argument("--no-authority-scan", action="store_true", help="Timing experiment: omit whole-RAM diagnostics; targeted state checks remain mandatory")
    parser.add_argument("--profile-rt64", action="store_true", help="Compile copied RT64 full-sync phase timers")
    parser.add_argument("--no-owned-readback", action="store_true", help="Experiment: disable RT64 RAM readback only for owned tasks")
    parser.add_argument("--shadow-eye-offset", type=float, default=0,
                        help="Controlled eye-X perturbation in owned RAM only (shadow-snapshot or shadow120)")
    args = parser.parse_args()
    if not 6 <= args.trials <= 300:
        parser.error("--trials must be 6..300; increase --timeout for long runs")
    endpoint_mode = args.mode == "endpoint120"
    motion_mode = args.mode == "motion120" or endpoint_mode
    pose_mode = args.mode == "pose-snapshot" or motion_mode
    if pose_mode or args.no_authority_scan:
        args.require_repeatable = True
    owned_ram = args.mode in ("shadow-snapshot", "shadow120", "pose-snapshot", "motion120", "endpoint120")
    independent_present = args.mode in ("present120", "shadow120", "motion120", "endpoint120")
    render_only = owned_ram or independent_present or args.mode in ("render-only", "camera-snapshot")
    if args.no_authority_scan and not owned_ram:
        parser.error("--no-authority-scan requires an owned-RAM mode")
    if args.no_owned_readback and not owned_ram:
        parser.error("--no-owned-readback requires an owned-RAM mode")
    if not math.isfinite(args.shadow_eye_offset) or abs(args.shadow_eye_offset) > 10 or (
            args.shadow_eye_offset and (not owned_ram or pose_mode)):
        parser.error("--shadow-eye-offset requires shadow-snapshot or shadow120 and a finite value within +/-10")
    if not 60 <= args.settle_frames <= 1800:
        parser.error("--settle-frames must be 60..1800; increase --timeout for long runs")
    if args.require_repeatable and args.mode not in ("repeat", "render-only", "present120", "camera-snapshot", "shadow-snapshot", "shadow120", "pose-snapshot", "motion120", "endpoint120"):
        parser.error("--require-repeatable requires a repeat, render-only, presentation or snapshot mode")
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
                    *(["-DWDC_PROTOTYPE_PRESENT"] if independent_present else []),
                    "-I", str(source / "lib/N64ModernRuntime/N64Recomp/include"),
                    "-I", str(source / "lib/N64ModernRuntime/ultramodern/include"),
                    "-I", str(source / "src"),
                    "-c", str(here / "probe.cpp"), "-o", str(probe)], check=True)
    exe = output / "render_split_prototype.exe"
    if args.profile_rt64:
        objects.append(build_rt64_profile(reference, output, compiler))
    runtime = [str(build_runtime(reference, output, compiler))] if independent_present else []
    if owned_ram:
        replacement, original_object, queue_object = build_shadow_renderer(reference, output, compiler)
        if original_object not in [o.replace("\\", "/") for o in objects]:
            raise SystemExit("Reference renderer object missing from executable link")
        objects = [replacement if o.replace("\\", "/") == original_object else o for o in objects]
        objects.append(queue_object)
    if endpoint_mode:
        if "libWdcRecompiledFuncs.a" not in variable("LINK_LIBRARIES"):
            raise SystemExit("Expected reference translation archive in executable link")
        # This translation lives in an archive, unlike the renderer object.
        # The complete copied object satisfies its symbols before extraction.
        objects.append(build_draw_boundary(reference, output))
    wrapped = ["func_8000C560", "func_8001F9CC", "func_800214C8", "osSpTaskStartGo_recomp",
               "func_80043274", "func_80042FA8", "func_8002A3A8",
               "func_8001FC64", "func_8001F7F8",
               "func_80024038", "func_80010A5C", "func_80010ECC", "func_80024FCC", "func_80025350",
               "func_800013DC", "osRecvMesg_recomp", "osViSwapBuffer_recomp",
               "osSendMesg_recomp",
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
    if endpoint_mode:
        shutil.copy2(output / "draw-boundary-source.json", run_dir / "draw-boundary-source.json")
    if args.profile_rt64:
        shutil.copy2(output / "rt64-profile-source.json", run_dir / "rt64-profile-source.json")
    if runtime:
        shutil.copy2(output / "runtime-source.json", run_dir / "runtime-source.json")
    if owned_ram:
        shutil.copy2(output / "shadow-source.json", run_dir / "shadow-source.json")
    config = run_dir / "build" / "user-data"
    config.mkdir(parents=True, exist_ok=True)
    (config / "graphics.json").write_text(json.dumps({"resolution": 1, "native_60fps": False}))
    env = {key: value for key, value in os.environ.items() if not key.startswith("WDC_")}
    env.update(WDC_WARP=args.warp, WDC_RENDER_PROTOTYPE_MODE=args.mode,
               WDC_RENDER_PROTOTYPE_SETTLE=str(args.settle_frames),
               WDC_RENDER_PROTOTYPE_TRIALS=str(args.trials),
               WDC_RENDER_PROTOTYPE_SHADOW_OFFSET=str(args.shadow_eye_offset))
    if args.no_authority_scan:
        env["WDC_RENDER_PROTOTYPE_NO_AUTHORITY_SCAN"] = "1"
    if args.no_owned_readback:
        env["WDC_RENDER_PROTOTYPE_NO_READBACK"] = "1"
    (run_dir / "setup.json").write_text(json.dumps({"warp": args.warp, "mode": args.mode,
                                                  "settle_frames": args.settle_frames,
                                                  "trials": args.trials,
                                                  "profile_rt64": args.profile_rt64,
                                                  "authority_scan": not args.no_authority_scan,
                                                  "no_owned_readback": args.no_owned_readback,
                                                  "shadow_eye_offset": args.shadow_eye_offset}, indent=2))
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
    all_geometry = geometry
    if args.no_owned_readback:
        readbacks = [json.loads(line.split("[prototype-readback] ", 1)[1])
                     for line in log.read_text(errors="replace").splitlines() if "[prototype-readback] " in line]
        (run_dir / "readbacks.json").write_text(json.dumps(readbacks, indent=2))
        trial_frames = {r['frame'] for r in rows}
        expected = [(g['frame'], g['draw'], g['owned_memory']) for g in geometry if g['frame'] in trial_frames]
        observed = [(r['frame'], r['draw'], r['disabled']) for r in readbacks if r['frame'] in trial_frames]
        if not expected or observed != expected:
            raise SystemExit("Readback policy must disable only owned tasks and preserve every normal task")
    if owned_ram:
        # 252D0 drains each bank; 2523C then bootstraps the next bank with a
        # zero-geometry task. Retain those records in evidence, but compare the
        # actual full-frame tasks rather than treating bootstraps as frames.
        if any(not g['complete'] for g in geometry):
            raise SystemExit("Incomplete shadow task command stream")
        geometry = [g for g in geometry if g['triangles']]
    snapshots = [json.loads(line.split("[prototype-snapshot] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-snapshot] " in line]
    (run_dir / "snapshots.json").write_text(json.dumps(snapshots, indent=2))
    authority = [json.loads(line.split("[prototype-authority] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-authority] " in line]
    (run_dir / "authority.json").write_text(json.dumps(authority, indent=2))
    if args.no_authority_scan and authority:
        raise SystemExit("Full-RAM scan unexpectedly ran in the timing experiment")
    if args.no_authority_scan:
        print("TIMING EXPERIMENT: whole-RAM diagnostics omitted; targeted state/ownership checks retained")
    if owned_ram and not args.no_authority_scan and {(a['frame'], a['draw']) for a in authority} != {
            (s['frame'], s['draw']) for s in snapshots}:
        raise SystemExit("Missing full-RAM changed-range observations")
    projections = [json.loads(line.split("[prototype-projection] ", 1)[1])
                   for line in log.read_text(errors="replace").splitlines() if "[prototype-projection] " in line]
    (run_dir / "projections.json").write_text(json.dumps(projections, indent=2))
    presents = [json.loads(line.split("[prototype-present] ", 1)[1])
                for line in log.read_text(errors="replace").splitlines() if "[prototype-present] " in line]
    (run_dir / "presents.json").write_text(json.dumps(presents, indent=2))
    queue_tests = [json.loads(line.split("[prototype-queue-test] ", 1)[1])
                   for line in log.read_text(errors="replace").splitlines() if "[prototype-queue-test] " in line]
    if owned_ram and queue_tests != [{
            "authoritative_sentinel_preserved": True, "owned_result_received": True}]:
        raise SystemExit("Owned receive-result sentinel test did not pass")
    phases = [json.loads(line.split("[prototype-phase] ", 1)[1])
              for line in log.read_text(errors="replace").splitlines() if "[prototype-phase] " in line]
    (run_dir / "phases.json").write_text(json.dumps(phases, indent=2))
    if args.profile_rt64:
        expected_syncs = sorted((g['frame'], g['draw']) for g in geometry if g['draw'] and g['triangles'])
        observed_syncs = sorted((p['frame'], p['draw']) for p in phases if p['phase'] == 'rt64_full_sync')
        if not expected_syncs or observed_syncs != expected_syncs:
            raise SystemExit("Expected one measured RT64 full sync per complete extra frame")
    if phases:
        for name in sorted({p['phase'] for p in phases}):
            values = [p['ms'] for p in phases if p['phase'] == name]
            print(f"{name}: mean {statistics.mean(values):.3f} ms, max {max(values):.3f} ms ({len(values)} calls)")
    for row in rows:
        print(f"trial {row['trial']}: extra draw {row['draw_ms']:.1f} ms; "
              f"samples {row['sample_before']} -> {row['sample_after']}; "
              f"sound buffer {row['sound_buffer_before']} -> {row['sound_buffer_after']}; "
              f"changed car words {len(row['car_word_changes'])}")
    if result.returncode or len(rows) != args.trials:
        raise SystemExit(f"Incomplete probe (exit {result.returncode}); inspect {log}")
    if any(row['sample_before'] != row['sample_after'] or
           row['divider_before'] != 1 or row['divider_after'] != 1 or
           row['ticks_before'] != 1562500 or row['ticks_after'] != 1562500 for row in rows):
        raise SystemExit("Simulation counter or stock sampling contract changed")
    repeats = 3 if endpoint_mode else 4 if motion_mode else 3 if independent_present or pose_mode else (2 if args.mode in ("repeat", "render-only", "camera-snapshot", "shadow-snapshot") else 1)
    if args.mode != "raw" and any(row['skipped_publish'] != row['trial'] * repeats or
                                          row['skipped_consume'] != row['trial'] * repeats for row in rows):
        raise SystemExit("Sound wrappers did not intercept every extra draw")
    if render_only and any(row['skipped_audio_build'] != row['trial'] * repeats or
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
            if endpoint_mode:
                if group[0]['owned_memory']:
                    raise SystemExit("Current endpoint must use the original authoritative draw")
            elif motion_mode:
                if not group[4]['same_headers_as_normal']:
                    raise SystemExit("Motion endpoint must reproduce normal geometry")
            elif pose_mode:
                if not group[2]['same_headers_as_first_extra'] or not group[3]['same_headers_as_normal']:
                    raise SystemExit("Pose midpoint must repeat exactly and current endpoint must match normal")
            elif args.shadow_eye_offset:
                if any(g['same_headers_as_normal'] for g in group[1:]) or any(not g['same_headers_as_first_extra'] for g in group[1:]):
                    raise SystemExit("Owned eye perturbation must change normal geometry and repeat across extras")
            elif any(not g['same_headers_as_normal'] for g in group[1:]):
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
            if owned_ram and (any(not g['owned_memory'] for g in group[1:]) or
                    any(not s['shadow_receives'] for s in states)):
                raise SystemExit("Owned RAM / authoritative queue routing was not observed")
            if owned_ram:
                for draw in range(1, repeats + 1):
                    tasks = [g for g in all_geometry if g['frame'] == frame and g['draw'] == draw]
                    if len(tasks) != 2 or sum(g['triangles'] == 0 for g in tasks) != 1 or any(not g['owned_memory'] for g in tasks):
                        raise SystemExit("Expected one owned bootstrap and one full-frame task per shadow draw")
            if render_only and any(
                    not s['preserved_flare_calls'] for s in states):
                raise SystemExit("Lens-flare preservation did not intercept each extra draw")
            if render_only and any(
                    s['skipped_car_sound_calls'] != s['car_count'] for s in states):
                raise SystemExit("Single-view car sound gating did not intercept every car per extra draw")
            if render_only and any(
                    change[0] % 0x1418 not in (0x360, 0x364) and not (
                        args.mode == "camera-snapshot" and change[0] in (0x18, 0x68, 0x6C)
                        and change[1] - change[2] == 128)
                    for s in states for change in s['view_word_changes']):
                raise SystemExit("Render-only draw changed per-view words outside flare render pointers")
            matrices = [p for p in projections if p['frame'] == frame]
            if len(matrices) != repeats + 1 or {p['draw'] for p in matrices} != set(range(repeats + 1)) or any(
                    p['matrix'] != matrices[0]['matrix'] for p in matrices):
                raise SystemExit("Missing or unequal single-view projection evidence")
        print("PASS: " +
              ("original endpoint retained; " if endpoint_mode else "motion endpoint matches; " if motion_mode else "pose midpoint repeats and endpoint matches; " if pose_mode else
               "owned eye offset changes normal geometry; " if args.shadow_eye_offset else "normal geometry matches; ") +
              "projection, camera snapshot, all-car non-link words and caller context preserved")
    if pose_mode:
        poses = [json.loads(line.split("[prototype-pose] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-pose] " in line]
        (run_dir / "poses.json").write_text(json.dumps(poses, indent=2))
        expected = {(r['frame'], d) for r in rows for d in range(1, repeats + 1)}
        if len(poses) != len(expected) or {(p['frame'], p['draw']) for p in poses} != expected:
            raise SystemExit("Missing pose interpolation observations")
        if any(p['alpha'] != (p['draw'] / 4 if motion_mode else 1 if p['draw'] == 3 else .5) or
               (not endpoint_mode and p['draw'] == repeats and p['changed_poses']) for p in poses):
            raise SystemExit("Invalid pose alpha or changed current endpoint")
        if not any(p['changed_poses'] for p in poses) or not any(
                (g['draw'] == 0 and not g['same_headers_as_first_extra'] if endpoint_mode else
                 g['draw'] == 1 and not g['same_headers_as_normal']) for g in geometry):
            raise SystemExit("Pose experiment did not demonstrate intermediate geometry")
        print("PASS: interpolated camera/body inputs change geometry; " +
              ("original draw supplies current endpoint" if endpoint_mode else "current endpoint matches normal"))
        if motion_mode:
            expected_order = [(r['frame'], d) for r in rows for d in ((1, 2, 3, 0) if endpoint_mode else range(1, 5))]
            if [(p['frame'], p['draw']) for p in presents] != expected_order:
                raise SystemExit("Motion presentations missing, reordered, or include a normal draw")
            print("PASS: quarter-step poses and current endpoint presented in chronological order")
    if endpoint_mode:
        endpoints = [json.loads(line.split("[prototype-endpoint] ", 1)[1])
                     for line in log.read_text(errors="replace").splitlines() if "[prototype-endpoint] " in line]
        (run_dir / "endpoints.json").write_text(json.dumps(endpoints, indent=2))
        if [e['frame'] for e in endpoints] != [r['frame'] for r in rows] or any(
                e['sample_before'] != e['sample_after'] or
                any(e[k] != 1 for k in ('publish', 'consume', 'audio_build', 'audio_retire')) for e in endpoints):
            raise SystemExit("Each group must retain one original audio lifecycle and consume no samples during drawing")
        for row in rows:
            tasks = [g for g in all_geometry if g['frame'] == row['frame'] and g['draw'] == 0]
            if len(tasks) != 2 or sum(g['triangles'] == 0 for g in tasks) != 1 or any(g['owned_memory'] for g in tasks):
                raise SystemExit("Expected one authoritative bootstrap and endpoint task")
        print("PASS: one authoritative endpoint/audio lifecycle replaces the redundant owned endpoint")
    if independent_present:
        wait_rows = endpoints if endpoint_mode else rows
        skipped_deltas = [b['skipped_vi_waits'] - a for a, b in zip(
            [0] + [r['skipped_vi_waits'] for r in wait_rows[:-1]], wait_rows)]
        # Suppressing the normal swap can trigger 013DC's additional receive
        # at 015E8 when osViGetCurrentFramebuffer differs from the game target.
        valid_waits = all(10 <= delta <= 11 for delta in skipped_deltas) if motion_mode else all(
            delta == 8 for delta in skipped_deltas)
        if endpoint_mode:
            valid_waits = 8 <= skipped_deltas[0] <= 11 and all(8 <= d <= 9 for d in skipped_deltas[1:])
        if len(presents) != 4 * args.trials or not valid_waits:
            raise SystemExit("Missing direct presentation callbacks or swap-wait interception")
        intervals = [(b['us'] - a['us']) / 1000 for a, b in zip(presents, presents[1:])]
        rate = 1000 / statistics.mean(intervals)
        vi_ticks = presents[-1]['vi_ticks'] - presents[0]['vi_ticks']
        print(f"Renderer callbacks: {rate:.1f}/s; intervals {min(intervals):.2f}..{max(intervals):.2f} ms; {vi_ticks} VI ticks")
        expected_vi_ticks = sum(intervals) * 60 / 1000
        if not 105 <= rate <= 135 or abs(vi_ticks - expected_vi_ticks) > 2:
            raise SystemExit("120 Hz presentation / 60 Hz VI timing gate failed")


if __name__ == "__main__":
    main()

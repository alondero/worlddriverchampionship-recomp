"""THROWAWAY Windows/MinGW probe: relink an existing player without rebuilding it.

The reference build is read-only. All new objects, executable, settings and
evidence live under this worktree's ignored build/render-split-prototype.
GNU --wrap intercepts cross-object calls; runtime counters establish that the
sound and presentation boundaries were intercepted rather than optimized away.
"""
import argparse
from fractions import Fraction
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
import sys
import tempfile
from runtime import build_runtime
from shadow import build_shadow_renderer
from rt64_profile import build_rt64_profile
from fast_hash_build import build_fast_hash
from draw_boundary import build_draw_boundary
from present_audit import build_present_audit, validate_admissions, validate_swaps
from swap_timing import group_intervals, summarize_swaps
from present_latency import join_present_latency


def display_rate(value):
    try:
        return Fraction(value)
    except (ValueError, ZeroDivisionError) as exc:
        raise argparse.ArgumentTypeError("Expected Hz as a number or numerator/denominator") from exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--mode", choices=["raw", "sound-gated", "camera-offset", "repeat", "render-only", "split60", "present120", "camera-snapshot", "shadow-snapshot", "shadow120", "shadow-rate", "pose-snapshot", "motion120", "motion-rate", "motion-samples", "endpoint120"], default="sound-gated")
    parser.add_argument("--require-repeatable", action="store_true",
                        help="Fail unless repeat's normal and both extra draws have equal triangle headers and frozen camera/car checks pass")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--warp", default="1:0", help="Player race setup: track[:car[:mode[:laps]]]")
    parser.add_argument("--settle-frames", type=int, default=60, help="Eligible normal draws before trials")
    parser.add_argument("--shadow-inputs", nargs="?", const="all", choices=["all", "only"], help="Interpolate shadow ground inputs; only isolates them in pose-snapshot")
    parser.add_argument("--wheel-poses", nargs="?", const="all", choices=["all", "only"], help="Interpolate four wheel matrices per car; only is a pose-snapshot geometry control")
    parser.add_argument("--reuse-endpoints", action="store_true", help="motion-samples: draw at the original dispatcher boundary and present the authoritative endpoint when it is on the display grid")
    parser.add_argument("--skip-late-draws", action="store_true", help="motion-samples: skip elapsed group slots before starting another draw")
    parser.add_argument("--present-hz", type=display_rate, default=Fraction(120), help="Display Hz, e.g. 144, 59.94 or 120000/1001")
    parser.add_argument("--trials", type=int, default=6, help="Bounded trial count, 6..1800")
    parser.add_argument("--no-authority-scan", action="store_true", help="Timing experiment: omit whole-RAM diagnostics; targeted state checks remain mandatory")
    parser.add_argument("--notify-renderer-completion", action="store_true", help="Experiment: wake the owned-bank fence when renderer task/callback completion advances")
    parser.add_argument("--write-watch-reset", action="store_true", help="Windows experiment: restore tracked dirty pages between draws of one snapshot")
    parser.add_argument("--verify-write-watch", action="store_true", help="Compare every restored byte with the frozen bank; requires --write-watch-reset")
    parser.add_argument("--fast-texture-hash", action="store_true", help="Experiment: stop saturated palette scans and group contiguous palette hash updates")
    parser.add_argument("--cache-palette-indices", action="store_true", help="Experiment: cache palette index membership using exact texture-byte matches; requires fast texture hash")
    parser.add_argument("--async-diagnostics", action="store_true", help="Experiment: bounded asynchronous diagnostic writes with final drain audit")
    parser.add_argument("--batch-texture-hash", action="store_true", help="Experiment: hash a bounded concatenation of the identical byte stream; requires fast texture hash")
    parser.add_argument("--verify-texture-hash", action="store_true", help="Compare every optimized hash with the original RT64 hasher")
    parser.add_argument("--hold-accelerator", action="store_true", help="Input fixture: hold port-0 A with neutral steering during the race")
    parser.add_argument("--capture-frames", action="store_true", help="Capture swap images; requires explicit present and exactly six trials; timing is diagnostic only")
    parser.add_argument("--capture-boss-tasks", action="store_true", help="Capture and replay the first owned bootstrap/full task pair; requires pose-snapshot and six trials")
    parser.add_argument("--explicit-present", action="store_true", help="Submit every explicit prototype screen update to RT64; requires --audit-present")
    parser.add_argument("--pace-present-queue", action="store_true", help="Experiment: wait for the previous event to be dequeued before submitting another; requires explicit present without capture")
    parser.add_argument("--present-dequeue-delay-ms", type=int, default=0, help="Slow-consumer fixture: delay dequeue notification 0..100 ms; requires explicit present and six trials without capture")
    parser.add_argument("--audit-present", action="store_true", help="Observe actual RT64 swap calls and bind presentation IDs to prototype labels")
    parser.add_argument("--profile-rt64", action="store_true", help="Compile copied RT64 full-sync phase timers")
    parser.add_argument("--sample-render-worker", action="store_true", help="Windows diagnostic: sample the graphics worker during owned BOSS tasks")
    parser.add_argument("--no-owned-readback", action="store_true", help="Experiment: disable RT64 RAM readback only for owned tasks")
    parser.add_argument("--shadow-eye-offset", type=float, default=0,
                        help="Controlled eye-X perturbation in owned RAM only (shadow-snapshot or shadow120)")
    args = parser.parse_args()
    if not 6 <= args.trials <= 1800:
        parser.error("--trials must be 6..1800; increase --timeout for long runs")
    if args.capture_boss_tasks and (args.mode != "pose-snapshot" or args.trials != 6):
        parser.error("--capture-boss-tasks requires pose-snapshot and six trials")
    if args.verify_texture_hash and not args.fast_texture_hash:
        parser.error("--verify-texture-hash requires --fast-texture-hash")
    if args.cache_palette_indices and not args.fast_texture_hash:
        parser.error("--cache-palette-indices requires --fast-texture-hash")
    if args.batch_texture_hash and not args.fast_texture_hash:
        parser.error("--batch-texture-hash requires --fast-texture-hash")
    sample_motion = args.mode == "motion-samples"
    if args.skip_late_draws and not sample_motion:
        parser.error("--skip-late-draws requires motion-samples")
    motion_rate = args.mode == "motion-rate" or sample_motion
    cadence_mode = args.mode == "shadow-rate" or motion_rate
    if not cadence_mode and args.present_hz != 120:
        parser.error("--present-hz requires a cadence mode")
    if not 30 < args.present_hz <= 240 or args.present_hz.denominator > 1000000:
        parser.error("Motion cadence requires 30 < Hz <= 240 and denominator <= 1000000")
    if args.mode == "shadow-rate" and args.present_hz < 60:
        parser.error("Frozen cadence requires at least one extra per group (60 Hz)")
    if args.reuse_endpoints and not sample_motion:
        parser.error("--reuse-endpoints requires motion-samples")
    endpoint_mode = args.mode == "endpoint120" or args.reuse_endpoints
    motion_mode = args.mode == "motion120" or motion_rate or endpoint_mode
    pose_mode = args.mode == "pose-snapshot" or motion_mode
    if args.shadow_inputs and not pose_mode:
        parser.error("--shadow-inputs requires a pose mode")
    if args.shadow_inputs == "only" and (args.mode != "pose-snapshot" or args.wheel_poses):
        parser.error("--shadow-inputs only requires pose-snapshot without wheel options")
    if args.wheel_poses == "only" and args.shadow_inputs:
        parser.error("Wheel-only and shadow-input controls cannot be combined")
    if args.wheel_poses == "only" and args.mode != "pose-snapshot":
        parser.error("--wheel-poses only requires pose-snapshot")
    if args.wheel_poses and not pose_mode:
        parser.error("--wheel-poses requires a pose interpolation mode")
    if cadence_mode or pose_mode or args.no_authority_scan or args.fast_texture_hash:
        args.require_repeatable = True
    owned_ram = args.mode in ("shadow-snapshot", "shadow120", "shadow-rate", "pose-snapshot", "motion120", "motion-rate", "motion-samples", "endpoint120")
    independent_present = args.mode in ("present120", "shadow120", "shadow-rate", "motion120", "motion-rate", "motion-samples", "endpoint120")
    if args.capture_frames and (not args.explicit_present or args.trials != 6):
        parser.error("--capture-frames requires --explicit-present and --trials 6")
    if args.explicit_present and not args.audit_present:
        parser.error("--explicit-present requires --audit-present")
    if args.pace_present_queue and (not args.explicit_present or args.capture_frames):
        parser.error("--pace-present-queue requires --explicit-present without --capture-frames")
    if not 0 <= args.present_dequeue_delay_ms <= 100 or (args.present_dequeue_delay_ms and (
            not args.explicit_present or args.capture_frames or args.trials != 6)):
        parser.error("--present-dequeue-delay-ms requires 0..100 ms, --explicit-present, six trials and no capture")
    if args.audit_present:
        args.require_repeatable = True
    if args.audit_present and not (owned_ram and independent_present):
        parser.error("--audit-present requires owned RAM and independent presentation")
    render_only = owned_ram or independent_present or args.mode in ("render-only", "camera-snapshot")
    if args.no_authority_scan and not owned_ram:
        parser.error("--no-authority-scan requires an owned-RAM mode")
    if args.no_owned_readback and not owned_ram:
        parser.error("--no-owned-readback requires an owned-RAM mode")
    if args.notify_renderer_completion:
        args.require_repeatable = True
        if not owned_ram:
            parser.error("--notify-renderer-completion requires an owned-RAM mode")
    if args.verify_write_watch and not args.write_watch_reset:
        parser.error("--verify-write-watch requires --write-watch-reset")
    if args.write_watch_reset:
        args.require_repeatable = True
        if not owned_ram:
            parser.error("--write-watch-reset requires an owned-RAM mode")
    if not math.isfinite(args.shadow_eye_offset) or abs(args.shadow_eye_offset) > 10 or (
            args.shadow_eye_offset and (not owned_ram or pose_mode)):
        parser.error("--shadow-eye-offset requires shadow-snapshot or shadow120 and a finite value within +/-10")
    if not 60 <= args.settle_frames <= 1800:
        parser.error("--settle-frames must be 60..1800; increase --timeout for long runs")
    if args.require_repeatable and args.mode not in ("repeat", "render-only", "present120", "camera-snapshot", "shadow-snapshot", "shadow120", "shadow-rate", "pose-snapshot", "motion120", "motion-rate", "motion-samples", "endpoint120"):
        parser.error("--require-repeatable requires a repeat, render-only, presentation or snapshot mode")
    reference = args.reference_build.resolve()
    if args.sample_render_worker and not owned_ram:
        parser.error("--sample-render-worker requires an owned-RAM mode")
    if args.capture_boss_tasks and not (reference / "wdc_boss_bench.exe").is_file():
        parser.error("--capture-boss-tasks requires wdc_boss_bench.exe in the reference build")
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
                    *(["-DWDC_PROTOTYPE_CAPTURE"] if args.capture_frames else []),
                    *(["-DWDC_PROTOTYPE_CAPTURE_BOSS"] if args.capture_boss_tasks else []),
                    *(["-DWDC_PROTOTYPE_SAMPLE_WORKER"] if args.sample_render_worker else []),
                    *(["-DWDC_PROTOTYPE_PRESENT_ADMISSION"] if args.pace_present_queue else []),
                    "-I", str(source / "lib/N64ModernRuntime/N64Recomp/include"),
                    "-I", str(source / "lib/N64ModernRuntime/ultramodern/include"),
                    "-I", str(here.parents[1]), "-I", str(source / "src"),
                    "-c", str(here / "probe.cpp"), "-o", str(probe)], check=True)
    exe = output / "render_split_prototype.exe"
    if args.fast_texture_hash:
        objects.append(build_fast_hash(reference, output, compiler, args.cache_palette_indices, args.batch_texture_hash))
    if args.audit_present:
        objects.append(build_present_audit(reference, output, compiler, args.capture_frames))
    if args.profile_rt64 or args.explicit_present:
        objects.append(build_rt64_profile(reference, output, compiler, profile=args.profile_rt64, explicit_present=args.explicit_present))
    runtime = [str(build_runtime(reference, output, compiler))] if independent_present else []
    if owned_ram:
        replacement, original_object, queue_object = build_shadow_renderer(reference, output, compiler, args.audit_present)
        if original_object not in [o.replace("\\", "/") for o in objects]:
            raise SystemExit("Reference renderer object missing from executable link")
        objects = [replacement if o.replace("\\", "/") == original_object else o for o in objects]
        objects.append(queue_object)
    if endpoint_mode:
        if "libWdcRecompiledFuncs.a" not in variable("LINK_LIBRARIES"):
            raise SystemExit("Expected reference translation archive in executable link")
        boundary_object = build_draw_boundary(reference, output)
        if boundary_object:
            objects.append(boundary_object)
    wrapped = ["wdc_hud_normal_layout", "osContGetReadData_recomp", "func_8003062C", "func_8001A97C", "func_8000C560", "func_8001F9CC", "func_800214C8", "osSpTaskStartGo_recomp",
               "func_80043274", "func_80042FA8", "func_8002A3A8",
               "func_8001FC64", "func_8001F7F8",
               "func_80024038", "func_80010A5C", "func_80010ECC", "func_80024FCC", "func_80025350",
               "func_800013DC", "osRecvMesg_recomp", "osViSwapBuffer_recomp",
               "osSendMesg_recomp", "_Z13rsp_read_cop0j", "_Z18wdc_boss_dma_writePhjjj",
               "wdc_framerate_preswap", "wdc_projection_normalize", "_ZN3wdc3rsp3runEPhPK6OSTaskPFvPvS1_jjES5_"]
    if endpoint_mode and not boundary_object:
        wrapped.append("wdc_render_race_draw")
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
    if args.sample_render_worker:
        shutil.copy2(exe, run_dir / "sampled-renderer.exe")
    if args.fast_texture_hash:
        shutil.copy2(output / "fast-hash-source.json", run_dir / "fast-hash-source.json")
    if endpoint_mode:
        shutil.copy2(output / "draw-boundary-source.json", run_dir / "draw-boundary-source.json")
    if args.audit_present:
        shutil.copy2(output / "present-audit-source.json", run_dir / "present-audit-source.json")
    if args.profile_rt64 or args.explicit_present:
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
               WDC_RENDER_PROTOTYPE_PRESENT_HZ=str(args.present_hz.numerator),
               WDC_RENDER_PROTOTYPE_RATE_DENOMINATOR=str(args.present_hz.denominator),
               WDC_RENDER_PROTOTYPE_SHADOW_OFFSET=str(args.shadow_eye_offset))
    if args.hold_accelerator:
        env["WDC_RENDER_PROTOTYPE_ACCELERATOR"] = "1"
    if args.shadow_inputs:
        env["WDC_RENDER_PROTOTYPE_SHADOW_INPUTS"] = "1"
    if args.shadow_inputs == "only":
        env["WDC_RENDER_PROTOTYPE_SHADOW_ONLY"] = "1"
    if args.wheel_poses == "only":
        env["WDC_RENDER_PROTOTYPE_WHEEL_ONLY"] = "1"
    if args.wheel_poses:
        env["WDC_RENDER_PROTOTYPE_WHEEL_POSES"] = "1"
    if args.reuse_endpoints:
        env["WDC_RENDER_PROTOTYPE_REUSE_ENDPOINTS"] = "1"
    if args.skip_late_draws:
        env["WDC_RENDER_PROTOTYPE_SKIP_LATE_DRAWS"] = "1"
    if args.verify_texture_hash:
        env["WDC_RENDER_PROTOTYPE_VERIFY_TEXTURE_HASH"] = "1"
    if args.async_diagnostics:
        env["WDC_RENDER_PROTOTYPE_ASYNC_DIAGNOSTICS"] = "1"
    if args.no_authority_scan:
        env["WDC_RENDER_PROTOTYPE_NO_AUTHORITY_SCAN"] = "1"
    if args.notify_renderer_completion:
        env["WDC_RENDER_PROTOTYPE_NOTIFY_COMPLETION"] = "1"
    if args.write_watch_reset:
        env["WDC_RENDER_PROTOTYPE_WRITE_WATCH"] = "1"
    if args.verify_write_watch:
        env["WDC_RENDER_PROTOTYPE_VERIFY_WRITE_WATCH"] = "1"
    if args.no_owned_readback:
        env["WDC_RENDER_PROTOTYPE_NO_READBACK"] = "1"
    env["WDC_RENDER_PROTOTYPE_DEQUEUE_DELAY_MS"] = str(args.present_dequeue_delay_ms)
    if args.capture_boss_tasks:
        env["WDC_BOSS_CAPTURE"] = str(run_dir / "owned-task")
    (run_dir / "setup.json").write_text(json.dumps({"warp": args.warp, "mode": args.mode,
                                                  "settle_frames": args.settle_frames,
                                                  "trials": args.trials,
                                                  "present_hz": float(args.present_hz),
                                                  "present_rate_ratio": str(args.present_hz),
                                                  "skip_late_draws": args.skip_late_draws,
                                                  "reuse_endpoints": args.reuse_endpoints,
                                                  "wheel_poses": args.wheel_poses,
                                                  "shadow_inputs": args.shadow_inputs,
                                                  "profile_rt64": args.profile_rt64,
                                                  "sample_render_worker": args.sample_render_worker,
                                                  "audit_present": args.audit_present,
                                                  "explicit_present": args.explicit_present,
                                                  "pace_present_queue": args.pace_present_queue,
                                                  "present_dequeue_delay_ms": args.present_dequeue_delay_ms,
                                                  "capture_frames": args.capture_frames,
                                                  "capture_boss_tasks": args.capture_boss_tasks,
                                                  "hold_accelerator": args.hold_accelerator,
                                                  "fast_texture_hash": args.fast_texture_hash,
                                                  "cache_palette_indices": args.cache_palette_indices,
                                                  "batch_texture_hash": args.batch_texture_hash,
                                                  "async_diagnostics": args.async_diagnostics,
                                                  "verify_texture_hash": args.verify_texture_hash,
                                                  "authority_scan": not args.no_authority_scan,
                                                  "notify_renderer_completion": args.notify_renderer_completion,
                                                  "write_watch_reset": args.write_watch_reset,
                                                  "verify_write_watch": args.verify_write_watch,
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
    if args.audit_present:
        # Preserve skip evidence even when the native final audit times out.
        skips = [json.loads(line.split("[prototype-present-skipped] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-present-skipped] " in line]
        (run_dir / "presentation-skips.json").write_text(json.dumps(skips, indent=2))
        if skips:
            print(f"RT64 skipped events (including startup): {len(skips)}; "
                  f"backlog {sum(s['backlog'] for s in skips)}, empty swap chain {sum(s['empty_swapchain'] for s in skips)}")
    if args.async_diagnostics:
        diagnostics = [json.loads(line.split("[prototype-diagnostics] ", 1)[1])
                       for line in log.read_text(errors="replace").splitlines() if "[prototype-diagnostics] " in line]
        (run_dir / "diagnostics.json").write_text(json.dumps(diagnostics, indent=2))
        if (len(diagnostics) != 1 or diagnostics[0]["failed"] or not diagnostics[0]["accepted"] or
                diagnostics[0]["accepted"] != diagnostics[0]["written"] or
                diagnostics[0]["accepted_bytes"] != diagnostics[0]["written_bytes"] or
                not 0 < diagnostics[0]["max_queued_bytes"] <= 4 * 1024 * 1024):
            raise SystemExit("Asynchronous diagnostic drain audit failed")
    if result.returncode:
        raise SystemExit(f"Probe exited {result.returncode}; inspect {log}")
    if args.sample_render_worker:
        samples = run_dir / "worker.samples"
        binary = run_dir / "sampled-renderer.exe"
        symbol_env = dict(env)
        symbol_env["PATH"] = str(Path(compiler).parent) + os.pathsep + symbol_env.get("PATH", "")
        report = subprocess.run([sys.executable, str(here.parent / "boss/profile_report.py"), str(samples),
                                 "--exe", str(binary), "--asp", str(source / "src/aspMain.cpp"), "--top", "30"],
                                env=symbol_env, capture_output=True, text=True)
        (run_dir / "worker-profile.txt").write_text(report.stdout + report.stderr)
        (run_dir / "worker-profile.json").write_text(json.dumps({
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "samples_sha256": hashlib.sha256(samples.read_bytes()).hexdigest(),
            "symbolication_returncode": report.returncode}, indent=2))
        if report.returncode:
            raise SystemExit("Worker sample symbolication failed; inspect worker-profile.txt")
        functions = subprocess.run([sys.executable, str(here / "worker_profile.py"), str(samples),
                                    "--exe", str(binary)], env=symbol_env, capture_output=True, text=True)
        (run_dir / "worker-functions.txt").write_text(functions.stdout + functions.stderr)
        if functions.returncode:
            raise SystemExit("Worker function symbolication failed; inspect worker-functions.txt")
        print("Worker sampling is diagnostic; timing includes suspension overhead")
    if args.capture_boss_tasks:
        captures = sorted(run_dir.glob("owned-task-*.boss"))
        if len(captures) != 2:
            raise SystemExit("Expected the first owned bootstrap/full BOSS task pair")
        bench = reference / "wdc_boss_bench.exe"
        capture_evidence = {"bench_sha256": hashlib.sha256(bench.read_bytes()).hexdigest(), "captures": []}
        for capture in captures:
            replay = subprocess.run([str(bench), str(capture), "--iterations", "1"], env=env,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            capture.with_suffix(".replay.log").write_text(replay.stdout)
            if replay.returncode:
                raise SystemExit(f"Captured BOSS task failed exact RDP replay; inspect {capture.with_suffix('.replay.log')}")
            capture_evidence["captures"].append({"file": capture.name,
                "sha256": hashlib.sha256(capture.read_bytes()).hexdigest(), "replay_returncode": replay.returncode})
        (run_dir / "boss-captures.json").write_text(json.dumps(capture_evidence, indent=2))
        print("BOSS capture: bootstrap/full pair passed exact RDP replay")
    rows = [json.loads(line.split("[render-prototype] ", 1)[1])
            for line in log.read_text(errors="replace").splitlines() if "[render-prototype] " in line]
    (run_dir / "report.json").write_text(json.dumps(rows, indent=2))
    history = [json.loads(line.split("[prototype-history] ", 1)[1])
               for line in log.read_text(errors="replace").splitlines() if "[prototype-history] " in line]
    (run_dir / "history.json").write_text(json.dumps(history, indent=2))
    needles = [json.loads(line.split("[prototype-hud-needle] ", 1)[1])
               for line in log.read_text(errors="replace").splitlines() if "[prototype-hud-needle] " in line]
    (run_dir / "hud-needle.json").write_text(json.dumps(needles, indent=2))


    if args.hold_accelerator:
        inputs = [json.loads(line.split("[prototype-input] ", 1)[1])
                  for line in log.read_text(errors="replace").splitlines() if "[prototype-input] " in line]
        (run_dir / "inputs.json").write_text(json.dumps(inputs, indent=2))
        if [r["frame"] for r in inputs] != [r["frame"] for r in rows] or not inputs or inputs[0]["accelerator_samples"] == 0 or any(
                a["accelerator_samples"] >= b["accelerator_samples"] for a, b in zip(inputs, inputs[1:])):
            raise SystemExit("Scripted accelerator samples missing or not advancing")

    geometry = [json.loads(line.split("[prototype-rdp] ", 1)[1])
                for line in log.read_text(errors="replace").splitlines() if "[prototype-rdp] " in line]
    (run_dir / "geometry.json").write_text(json.dumps(geometry, indent=2))
    differences = [json.loads(line.split("[prototype-header-diff] ", 1)[1])
                   for line in log.read_text(errors="replace").splitlines() if "[prototype-header-diff] " in line]
    (run_dir / "header-differences.json").write_text(json.dumps(differences, indent=2))

    all_geometry = geometry
    if args.fast_texture_hash:
        hash_rows = [json.loads(line.split("[prototype-texture-hash] ", 1)[1])
                      for line in log.read_text(errors="replace").splitlines() if "[prototype-texture-hash] " in line]
        (run_dir / "texture-hash.json").write_text(json.dumps(hash_rows, indent=2))
        trial_frames = {r['frame'] for r in rows}
        hash_rows = [r for r in hash_rows if r['frame'] in trial_frames]
        expected = sorted((g['frame'], g['draw']) for g in geometry if g['frame'] in trial_frames and g['triangles'])
        observed = sorted((r['frame'], r['draw']) for r in hash_rows)
        if observed != expected:
            raise SystemExit("Missing texture hash evidence for complete trial tasks")
        calls = sum(r['calls'] for r in hash_rows)
        verified = sum(r['verified'] for r in hash_rows)
        if not calls or (args.verify_texture_hash and verified != calls):
            raise SystemExit("Texture hasher was not exercised or differential verification was incomplete")
        print(f"Optimized texture hash: {calls} calls; {verified} original-hasher comparisons")
        if args.cache_palette_indices:
            lookups = sum(r['palette_lookups'] for r in hash_rows)
            hits = sum(r['palette_hits'] for r in hash_rows)
            if not lookups or any(not 0 <= r['palette_hits'] <= r['palette_lookups'] for r in hash_rows):
                raise SystemExit("Missing or invalid palette-index cache observations")
            print(f"Palette index cache: {hits}/{lookups} exact-byte hits")
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
        if any(not 0 <= g['audio_dma_bytes'] <= g['dma_bytes'] or
               (g['triangles'] and g['dma_bytes'] == 0) or
               (g['owned_memory'] and g['audio_dma_bytes'] != 0) for g in geometry):
            raise SystemExit("Invalid BOSS DMA observations or owned task wrote the audio ring")
        # 252D0 drains each bank; 2523C then bootstraps the next bank with a
        # zero-geometry task. Retain those records in evidence, but compare the
        # actual full-frame tasks rather than treating bootstraps as frames.
        if any(not g['complete'] for g in geometry):
            raise SystemExit("Incomplete shadow task command stream")
        geometry = [g for g in geometry if g['triangles']]
        if not geometry or any(g['rsp_status_reads'] <= 0 or
                               not 0 <= g['rsp_status_repeats'] < g['rsp_status_reads'] for g in geometry):
            raise SystemExit("Missing or invalid BOSS status-read observations")
    snapshots = [json.loads(line.split("[prototype-snapshot] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-snapshot] " in line]
    (run_dir / "snapshots.json").write_text(json.dumps(snapshots, indent=2))
    if args.write_watch_reset:
        captures = [json.loads(line.split("[prototype-bank-capture] ", 1)[1])
                    for line in log.read_text(errors="replace").splitlines() if "[prototype-bank-capture] " in line]
        (run_dir / "bank-captures.json").write_text(json.dumps(captures, indent=2))
        if not captures or [c['frame'] for c in captures] != [r['frame'] for r in rows]:
            raise SystemExit("Missing or duplicate write-watch source captures")
        resets = [json.loads(line.split("[prototype-bank-reset] ", 1)[1])
                  for line in log.read_text(errors="replace").splitlines() if "[prototype-bank-reset] " in line]
        (run_dir / "bank-resets.json").write_text(json.dumps(resets, indent=2))
        if not resets or sorted((r['frame'], r['draw']) for r in resets) != sorted((s['frame'], s['draw']) for s in snapshots):
            raise SystemExit("Missing or duplicate write-watch resets")
        for r in captures + resets:
            page = r['page_size']
            if (not 0 < page <= 0x800000 or 0x800000 % page or
                not 0 <= r['copied_bytes'] <= 0x800000 or r['copied_bytes'] % page or
                r['compared_bytes'] not in (0, 0x800000) or
                r['verified'] != args.verify_write_watch):
                raise SystemExit("Invalid write-watch reset evidence")
        if any(c['copied_bytes'] != 0x800000 and c['compared_bytes'] != 0x800000 for c in captures):
            raise SystemExit("Write-watch capture did not cover the whole source")
        if resets[0]['copied_bytes'] != 0x800000 or any(r['compared_bytes'] for r in resets):
            raise SystemExit("Write-watch restore must seed once and avoid source rescans")
    if owned_ram and not endpoint_mode:
        if sorted((n["frame"], n["draw"]) for n in needles) != sorted((s["frame"], s["draw"]) for s in snapshots):
            raise SystemExit("Missing or duplicate single-view HUD needle snapshots")

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
    if args.audit_present:
        bindings = [json.loads(line.split("[prototype-present-id] ", 1)[1])
                    for line in log.read_text(errors="replace").splitlines() if "[prototype-present-id] " in line]
        swaps = [json.loads(line.split("[prototype-swap] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-swap] " in line]
        (run_dir / "present-bindings.json").write_text(json.dumps(bindings, indent=2))
        (run_dir / "swaps.json").write_text(json.dumps(swaps, indent=2))
        if args.pace_present_queue:
            admissions = [json.loads(line.split("[prototype-present-admission] ", 1)[1])
                          for line in log.read_text(errors="replace").splitlines() if "[prototype-present-admission] " in line]
            (run_dir / "present-admissions.json").write_text(json.dumps(admissions, indent=2))
            validate_admissions(admissions, bindings)
            print(f"Presentation admission wait: {sum(a['wait_us'] for a in admissions) / len(admissions):.1f} us mean; "
                  f"{max(a['wait_us'] for a in admissions)} us maximum")
        selected = validate_swaps(bindings, swaps, presents)
        latency = join_present_latency(bindings, selected)
        (run_dir / "present-latency.json").write_text(json.dumps(latency, indent=2))
        for phase in ("screen_to_dequeue_ms", "dequeue_to_ready_ms", "ready_to_api_ms", "swap_api_ms"):
            values = sorted(row[phase] for row in latency)
            print(f"Presentation {phase}: p95 {values[math.ceil(.95 * len(values)) - 1]:.3f}, max {values[-1]:.3f} ms")
        if args.capture_frames:
            captures = []
            for binding in bindings:
                path = run_dir / f"present-{binding['id']}-0.ppm"
                data = path.read_bytes()
                magic, dimensions, maximum, pixels = data.split(b"\n", 3)
                width, height = map(int, dimensions.split())
                if magic != b"P6" or maximum != b"255" or not (0 < width <= 4096 and 0 < height <= 4096) or len(pixels) != width*height*3:
                    raise SystemExit("Invalid captured image")
                captures.append(dict(binding, file=path.name, width=width, height=height,
                                     rgb_sha256=hashlib.sha256(pixels).hexdigest()))
            (run_dir / "captures.json").write_text(json.dumps(captures, indent=2))
            print(f"CAPTURE: {len(captures)} complete swap images; timing is diagnostic only")
            if not motion_mode and not args.shadow_eye_offset:
                for frame in {c["frame"] for c in captures}:
                    if len({c["rgb_sha256"] for c in captures if c["frame"] == frame}) != 1:
                        raise SystemExit("Frozen normal/extra swap pixels differ")
                print("PASS: frozen normal/extra swap images are pixel-identical")


        intervals = [(b["us"]-a["us"])/1000 for a, b in zip(selected, selected[1:])]
        swap_rate = 1000/statistics.mean(intervals)
        timing = summarize_swaps(selected, float(args.present_hz))
        timing.update(group_intervals(bindings, selected))
        (run_dir / "swap-timing.json").write_text(json.dumps(timing, indent=2))
        print(f"RT64 swap audit: {len(selected)} successful ordered swaps; "
              f"{swap_rate:.1f}/s; "
              f"intervals {min(intervals):.2f}..{max(intervals):.2f} ms (not physical scanout)")
        print(f"Swap intervals: p50 {timing['p50_ms']:.2f}, p95 {timing['p95_ms']:.2f}, "
              f"p99 {timing['p99_ms']:.2f} ms; {timing['short_intervals']} below 0.75 periods, "
              f"{timing['long_intervals']} above 1.5 periods (diagnostic, not a smoothness gate)")

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
        for stage in ("prepare", "tiles_and_calls", "upload_setup", "framebuffer_processing",
                      "materials_and_cache", "queue_and_finish"):
            observed = sorted((p['frame'], p['draw']) for p in phases
                              if p['phase'] == 'rt64_stage_' + stage)
            if observed != expected_syncs:
                raise SystemExit("Missing RT64 full-sync stage: " + stage)
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
    sample_schedules = {}
    if sample_motion:
        records = [json.loads(line.split("[prototype-schedule] ", 1)[1])
                   for line in log.read_text(errors="replace").splitlines() if "[prototype-schedule] " in line]
        (run_dir / "schedule.json").write_text(json.dumps(records, indent=2))
        if [r['frame'] for r in records] != [r['frame'] for r in rows]:
            raise SystemExit("Missing/duplicate snapshot schedule records")
        end = 0
        for i, (record, row) in enumerate(zip(records, rows)):
            if i == 0 and not record['reset']:
                raise SystemExit("First snapshot schedule must reset")
            gap = 1 if record['reset'] else (row['sample_before'] - rows[i-1]['sample_before']) & 0xffffffff
            if not 1 <= gap <= 4:
                raise SystemExit("Schedule did not reset on sample discontinuity")
            begin = 0 if record['reset'] else end
            end = begin + gap
            last = end * args.present_hz // 30
            available = last - begin * args.present_hz // 30
            count = min(available, math.ceil(args.present_hz / 30))
            expected = dict(frame=row['frame'], reset=record['reset'], begin=begin, end=end,
                            first_slot=last-count+1, count=count, skipped=available-count)
            if record != expected:
                raise SystemExit("Snapshot schedule disagrees with consumed samples or draw budget")
            sample_schedules[row['trial']] = expected
        print(f"Sample schedule: {sum(s['skipped'] for s in records)} stale grid slots skipped")
    def present_endpoint(trial):
        if args.reuse_endpoints:
            return sample_schedules[trial]['end'] * args.present_hz % 30 == 0
        return endpoint_mode

    def owned_slots(trial):
        return sample_schedules[trial]['count'] - int(present_endpoint(trial))

    selections = {}
    if args.skip_late_draws:
        records = [json.loads(line.split("[prototype-selection] ", 1)[1])
                   for line in log.read_text(errors="replace").splitlines() if "[prototype-selection] " in line]
        (run_dir / "selections.json").write_text(json.dumps(records, indent=2))
        if {r['frame'] for r in records} != {r['frame'] for r in rows}:
            raise SystemExit("Missing deadline selection groups")
        expected_order = []
        for row in rows:
            selected = [r for r in records if r['frame'] == row['frame']]
            schedule = sample_schedules[row['trial']]
            count = owned_slots(row["trial"])
            next_index, elapsed = 0, 0
            for draw, selection in enumerate(selected, 1):
                expected_order.append((row['frame'], draw))
                if selection['elapsed_ns'] < elapsed or next_index >= count:
                    raise SystemExit("Non-monotonic or surplus deadline selection")
                elapsed = selection['elapsed_ns']
                index = min(count-1, max(next_index, elapsed * args.present_hz // 1000000000))
                expected = dict(frame=row['frame'], draw=draw, elapsed_ns=elapsed, index=index,
                                slot=schedule['first_slot']+index, skipped=index-next_index)
                if selection != expected:
                    raise SystemExit("Deadline selection disagrees with elapsed time")
                next_index = index+1
            if next_index != count:
                raise SystemExit("Deadline skipping lost the latest scheduled pose")
            selections[row['trial']] = selected
        if [(r['frame'], r['draw']) for r in records] != expected_order:
            raise SystemExit("Deadline selection records reordered or duplicated")
        print(f"Deadline selection: {sum(r['skipped'] for r in records)} additional group slots skipped")
    def frame_repeats(trial):
        if args.skip_late_draws:
            return len(selections[trial])
        if sample_motion:
            return owned_slots(trial)
        return (trial * args.present_hz // 30 - (trial - 1) * args.present_hz // 30 - int(not motion_rate)) if cadence_mode else repeats
    trial_by_frame = {r['frame']: r['trial'] for r in rows}
    def pose_alpha(frame, draw):
        if sample_motion:
            s = sample_schedules[trial_by_frame[frame]]
            slot = selections[trial_by_frame[frame]][draw-1]['slot'] if args.skip_late_draws else s['first_slot'] + draw - 1
            return (slot * 30 - s['begin'] * args.present_hz) / ((s['end'] - s['begin']) * args.present_hz)
        if motion_rate:
            group = trial_by_frame[frame] - 1
            return ((group * args.present_hz // 30 + draw) * 30 - group * args.present_hz) / args.present_hz
        return draw / 4 if motion_mode else 1 if draw == 3 else .5
    def cumulative_repeats(trial):
        if sample_motion:
            return sum(frame_repeats(t) for t in range(1, trial+1))
        return trial * args.present_hz // 30 - (0 if motion_rate else trial) if cadence_mode else trial * repeats
    if args.mode != "raw" and any(row['skipped_publish'] != cumulative_repeats(row['trial']) or
                                          row['skipped_consume'] != cumulative_repeats(row['trial']) for row in rows):
        raise SystemExit("Sound wrappers did not intercept every extra draw")
    if render_only and any(row['skipped_audio_build'] != cumulative_repeats(row['trial']) or
            row['skipped_audio_retire'] != cumulative_repeats(row['trial']) or not row['preserved_flare_history'] for row in rows):
        raise SystemExit("Render-only audio/history wrappers did not intercept")
    for row in rows:
        count = frame_repeats(row['trial'])
        pair = sorted([g for g in geometry if g['frame'] == row['frame']], key=lambda g: g['draw'])
        if len(pair) != count + 1 or {g['draw'] for g in pair} != set(range(count + 1)) or any(not g['complete'] or not g['triangles'] for g in pair):
            raise SystemExit(f"Incomplete geometry evidence for frame {row['frame']}")
        print(f"frame {row['frame']}: normal/extra ordered triangle headers " +
              ("identical" if pair[0]['triangle_header_hash'] == pair[1]['triangle_header_hash'] else "DIFFERENT") +
              f"; triangle counts {[g['triangles'] for g in pair]}")
        if count >= 2:
            print("  consecutive extra draws: " +
                  ("identical" if pair[1]['triangle_header_hash'] == pair[2]['triangle_header_hash'] else "DIFFERENT"))
    if args.require_repeatable:
        frames = {row['frame'] for row in rows}
        for frame in frames:
            count = frame_repeats(next(r['trial'] for r in rows if r['frame'] == frame))
            group = sorted((g for g in geometry if g['frame'] == frame), key=lambda g: g['draw'])
            if endpoint_mode:
                if group[0]['owned_memory']:
                    raise SystemExit("Current endpoint must use the original authoritative draw")
            elif motion_mode:
                if pose_alpha(frame, count) == 1 and not group[count]['same_headers_as_normal']:
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
            if len(states) != count or {s['draw'] for s in states} != set(range(1, count + 1)):
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
                for draw in range(1, count + 1):
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
            if len(matrices) != count + 1 or {p['draw'] for p in matrices} != set(range(count + 1)) or any(
                    p['matrix'] != matrices[0]['matrix'] for p in matrices):
                raise SystemExit("Missing or unequal single-view projection evidence")
        print("PASS: " +
              ("original endpoint retained; " if endpoint_mode else "scheduled endpoints match when present; " if motion_rate else "motion endpoint matches; " if motion_mode else "pose midpoint repeats and endpoint matches; " if pose_mode else
               "owned eye offset changes normal geometry; " if args.shadow_eye_offset else "normal geometry matches; ") +
              "projection, camera snapshot, all-car non-link words and caller context preserved")
    if pose_mode:
        poses = [json.loads(line.split("[prototype-pose] ", 1)[1])
                 for line in log.read_text(errors="replace").splitlines() if "[prototype-pose] " in line]
        (run_dir / "poses.json").write_text(json.dumps(poses, indent=2))
        expected = {(r['frame'], d) for r in rows for d in range(1, frame_repeats(r['trial']) + 1)}
        if len(poses) != len(expected) or {(p['frame'], p['draw']) for p in poses} != expected:
            raise SystemExit("Missing pose interpolation observations")
        shadow_rows = []
        if args.shadow_inputs:
            shadow_rows = [json.loads(line.split("[prototype-shadow-inputs] ",1)[1])
                           for line in log.read_text(errors="replace").splitlines() if "[prototype-shadow-inputs] " in line]
            (run_dir / "shadow-inputs.json").write_text(json.dumps(shadow_rows,indent=2))
            if len(shadow_rows)!=len(expected) or {(s['frame'],s['draw']) for s in shadow_rows}!=expected:
                raise SystemExit("Missing shadow-input observations")
            if not any(s['changed'] for s in shadow_rows) or not any(s['rebuilds'] for s in shadow_rows):
                raise SystemExit("Shadow inputs/rebuilds were not exercised")
            if any(pose_alpha(s['frame'],s['draw'])==1 and s['changed'] for s in shadow_rows):
                raise SystemExit("Shadow current endpoint changed")
            for shadow in shadow_rows:
                state = next(s for s in snapshots if (s['frame'],s['draw']) == (shadow['frame'],shadow['draw']))
                width = 5 if shadow['contact_mode'] else 2
                if shadow['vectors']%width or shadow['vectors']>width*state['car_count'] or shadow['changed']>shadow['vectors'] or shadow['rebuilds']>state['car_count']:
                    raise SystemExit("Invalid shadow input/copy counts")
            if args.shadow_inputs=='only' and any(p['changed_poses'] for p in poses):
                raise SystemExit("Shadow-only control changed rigid poses")
            print("PASS: shadow ground inputs interpolate; original owned-bank rebuilds observed")
        if args.wheel_poses:
            wheel_reads = [json.loads(line.split("[prototype-wheel-reads] ", 1)[1])
                           for line in log.read_text(errors="replace").splitlines() if "[prototype-wheel-reads] " in line]
            (run_dir / "wheel-reads.json").write_text(json.dumps(wheel_reads, indent=2))
            if len(wheel_reads) != len(expected) or {(w['frame'], w['draw']) for w in wheel_reads} != expected or not any(w['copies'] for w in wheel_reads):
                raise SystemExit("Missing owned wheel-matrix copy observations")
            if args.wheel_poses == 'only' and any(p['changed_camera'] or p['changed_bodies'] for p in poses):
                raise SystemExit("Wheel-only control changed camera/body poses")
            if not any(p['changed_wheels'] for p in poses):
                raise SystemExit("Wheel poses did not interpolate")
            for p in poses:
                state = next(s for s in snapshots if (s['frame'],s['draw']) == (p['frame'],p['draw']))
                if p['poses'] != 1+5*state['car_count'] or p['changed_wheels'] > 4*state['car_count']:
                    raise SystemExit("Incomplete wheel pose snapshot")
            print("PASS: four wheel poses per car captured/interpolated; owned wheel copies observed")
        if sample_motion and any(p['reset'] != sample_schedules[trial_by_frame[p['frame']]]['reset'] for p in poses):
            raise SystemExit("Schedule reset differs from pose-history reset")
        if any(not math.isclose(p['alpha'], pose_alpha(p['frame'], p['draw']), rel_tol=0, abs_tol=1e-8) or
               (pose_alpha(p['frame'], p['draw']) == 1 and p['changed_poses']) for p in poses):
            raise SystemExit("Invalid pose alpha or changed current endpoint")
        if not (any(p['changed_poses'] for p in poses) or any(s['changed'] for s in shadow_rows)) or not any(
                (g['draw'] == 0 and not g['same_headers_as_first_extra'] if endpoint_mode else
                 g['draw'] == 1 and not g['same_headers_as_normal']) for g in geometry):
            raise SystemExit("Pose experiment did not demonstrate intermediate geometry")
        print("PASS: interpolated render inputs change geometry; " +
              ("original draw supplies current endpoint" if endpoint_mode else "scheduled endpoints match normal when present" if motion_rate else "current endpoint matches normal"))
        if motion_mode:
            expected_order = []
            for row in rows:
                trial, frame = row['trial'], row['frame']
                expected_order.extend((frame, d) for d in range(1, frame_repeats(trial) + 1))
                if present_endpoint(trial):
                    expected_order.append((frame, 0))
            if [(p['frame'], p['draw']) for p in presents] != expected_order:
                raise SystemExit("Motion presentations missing, reordered, or include a normal draw")
            print("PASS: scheduled poses presented in chronological order")
            if motion_rate and not sample_motion:
                gaps = [(b['sample_before'] - a['sample_before']) & 0xffffffff for a, b in zip(rows, rows[1:])]
                print(f"Nominal motion schedule: {sum(g == 1 for g in gaps)}/{len(gaps)} sample gaps are one; observed gaps {sorted(set(gaps))}")
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
        used = sum(present_endpoint(r['trial']) for r in rows)
        print(f"PASS: one authoritative audio lifecycle per group; {used} original draws supplied scheduled endpoints")
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
        expected_count = cumulative_repeats(args.trials) if sample_motion else args.trials * args.present_hz // 30 if cadence_mode else 4 * args.trials
        if args.reuse_endpoints:
            expected_count = cumulative_repeats(args.trials) + sum(present_endpoint(r['trial']) for r in rows)
            # Every owned draw and the normal drain traverse the swap waits.
            # The initial warm-up drain can add up to one extra swap sequence.
            valid_waits = all(2 * (frame_repeats(r['trial']) + 1) <= d <=
                              2 * (frame_repeats(r['trial']) + 1) + (3 if i == 0 else 1)
                              for i, (r, d) in enumerate(zip(rows, skipped_deltas)))
        elif cadence_mode:
            valid_waits = all(2 * (frame_repeats(r['trial']) + 1) <= d <= 2 * (frame_repeats(r['trial']) + 1) + int(motion_rate) for d, r in zip(skipped_deltas, rows))
            expected_order = [(r['frame'], d) for r in rows for d in range(int(motion_rate), frame_repeats(r['trial']) + 1)]
            if [(p['frame'], p['draw']) for p in presents] != expected_order:
                raise SystemExit("Rational cadence presentation order/count mismatch")
        if len(presents) != expected_count or not valid_waits:
            raise SystemExit("Missing direct presentation callbacks or swap-wait interception")
        intervals = [(b['us'] - a['us']) / 1000 for a, b in zip(presents, presents[1:])]
        rate = 1000 / statistics.mean(intervals)
        vi_ticks = presents[-1]['vi_ticks'] - presents[0]['vi_ticks']
        print(f"Renderer callbacks: {rate:.1f}/s; intervals {min(intervals):.2f}..{max(intervals):.2f} ms; {vi_ticks} VI ticks")
        expected_vi_ticks = sum(intervals) * 60 / 1000
        if abs(vi_ticks - expected_vi_ticks) > 2 or (not args.capture_frames and not args.present_hz * .875 <= rate <= args.present_hz * 1.125):
            raise SystemExit(f"{args.present_hz} Hz presentation / 60 Hz VI timing gate failed")
        if args.audit_present and not args.capture_frames and not args.present_hz * .875 <= swap_rate <= args.present_hz * 1.125:
            raise SystemExit(f"{args.present_hz} Hz actual RT64 swap timing gate failed")


if __name__ == "__main__":
    main()

"""Run a bounded controller scenario against a built WDC player and local USA ROM."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
ROM_NAME = "World Driver Championship (USA).z64"
ROM_SHA256 = "e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97"
MAX_BUDGET = 36000
BUTTONS = dict(
    A=0x8000, B=0x4000, Z=0x2000, START=0x1000,
    D_UP=0x0800, D_DOWN=0x0400, D_LEFT=0x0200, D_RIGHT=0x0100,
    L=0x20, R=0x10, C_UP=8, C_DOWN=4, C_LEFT=2, C_RIGHT=1,
)
COUNTS = ("menu_ticks", "graphics_tasks", "presentations")
RACE_IDENTITY = ("course", "layout", "car", "mode", "laps")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def fields(value, allowed, label):
    if not isinstance(value, dict) or value.keys() - set(allowed):
        raise ValueError(f"{label} must be an object with keys from {', '.join(allowed)}")


def integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{label} must be an integer from {minimum} to {maximum}")
    return value


def number(value, minimum, maximum, label):
    if type(value) not in (int, float) or not minimum <= value <= maximum or not math.isfinite(value):
        raise ValueError(f"{label} must be a finite number from {minimum} to {maximum}")
    return value


def validate(scenario):
    fields(scenario, ("schema", "name", "warp", "warp_at", "start", "max_vis", "input", "settings", "expect"),
           "scenario")
    integer(scenario.get("schema"), 1, 1, "schema")
    name = scenario.get("name")
    if not isinstance(name, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,64}", name):
        raise ValueError("name must be 1–64 letters, digits, hyphens or underscores")
    start = scenario.get("start", "race")
    if start not in ("boot", "menu", "race"):
        raise ValueError("start must be boot, menu or race")
    max_vis = integer(scenario.get("max_vis", 1800), 1, MAX_BUDGET, "max_vis")
    for key in ("warp", "warp_at"):
        if key in scenario and (not isinstance(scenario[key], str) or not scenario[key]):
            raise ValueError(f"{key} must be a nonempty string accepted by WDC's warp parser")

    runs = scenario.get("input")
    if not isinstance(runs, list) or not runs:
        raise ValueError("input must be a nonempty list of controller runs")
    lines = [f"WDC_SCENARIO_1 {start} {max_vis}"]
    polls = 0
    for run in runs:
        fields(run, ("polls", "buttons", "stick"), "input run")
        count = integer(run.get("polls"), 1, max_vis, "input polls")
        keys = run.get("buttons", [])
        if (not isinstance(keys, list) or any(not isinstance(key, str) or key not in BUTTONS for key in keys)
                or len(keys) != len(set(keys))):
            raise ValueError("buttons must be distinct N64 button names from " + ", ".join(BUTTONS))
        axes = run.get("stick", [0, 0])
        if not isinstance(axes, list) or len(axes) != 2:
            raise ValueError("stick must be [x, y]")
        x, y = (number(axis, -1, 1, "stick axis") for axis in axes)
        lines.append(f"{count} {sum(BUTTONS[key] for key in keys)} {x} {y}")
        polls += count
    if polls >= max_vis:
        raise ValueError("max_vis must allow time for input polls and the final dispatcher update")

    settings = scenario.get("settings", {})
    if not isinstance(settings, dict) or any(
        not re.fullmatch(r"[a-zA-Z0-9_-]+\.json", name) or not isinstance(value, dict)
        for name, value in settings.items()
    ):
        raise ValueError("settings must map JSON basenames to configuration objects")

    expect = scenario.get("expect")
    fields(expect, (*(f"min_{key}" for key in COUNTS), "races", "log_contains"), "expect")
    if not expect:
        raise ValueError("expect must assert at least one game outcome")
    for key in COUNTS:
        if f"min_{key}" in expect:
            integer(expect[f"min_{key}"], 1, 2**32 - 1, f"min_{key}")
    if "races" in expect:
        if not isinstance(expect["races"], list):
            raise ValueError("races must be an ordered list")
        for entry in expect["races"]:
            fields(entry, (*RACE_IDENTITY, "min_samples", "min_displacement",
                        "min_peak_visibility_list_count", "min_peak_visibility_filled_slots",
                        "min_frame_arena_gap", "min_secondary_frame_arena_gap"), "race expectation")
            if not entry:
                raise ValueError("each expected race must have at least one assertion")
            for key in (*RACE_IDENTITY, "min_samples",
                            "min_peak_visibility_list_count", "min_peak_visibility_filled_slots",
                            "min_frame_arena_gap", "min_secondary_frame_arena_gap"):
                if key in entry:
                    integer(entry[key], 0, 2**32 - 1, key)
            if "min_displacement" in entry:
                number(entry["min_displacement"], 0, 1e20, "min_displacement")
    if "log_contains" in expect and (
        not isinstance(expect["log_contains"], list) or not expect["log_contains"]
        or any(not isinstance(marker, str) or not marker for marker in expect["log_contains"])
    ):
        raise ValueError("log_contains must be a nonempty list of nonempty strings")
    return "\n".join(lines) + "\n", polls


def evaluate(expected, native, polls, log):
    errors = []
    if type(native) is not dict or native.get("schema") != 1 or native.get("input_polls") != polls:
        return ["Missing or invalid native result, or replay did not consume every input poll"]

    def minimum(observed, key, threshold, label):
        value = observed.get(key)
        if type(value) not in (int, float) or not threshold <= value < float("inf"):
            errors.append(f"{label}: expected at least {threshold}, observed {value}")

    for key in COUNTS:
        if f"min_{key}" in expected:
            minimum(native, key, expected[f"min_{key}"], key)
    if "races" in expected:
        races = native.get("races")
        if not isinstance(races, list) or len(races) != len(expected["races"]):
            errors.append(f"Expected {len(expected['races'])} race visits, observed {races}")
        else:
            for index, (wanted, observed) in enumerate(zip(expected["races"], races), 1):
                if not isinstance(observed, dict):
                    errors.append(f"Race {index} was malformed: {observed}")
                    continue
                for key, value in wanted.items():
                    if key.startswith("min_"):
                        minimum(observed, key[4:], value, f"Race {index} {key[4:]}")
                    elif observed.get(key) != value:
                        errors.append(f"Race {index} {key}: expected {value}, observed {observed.get(key)}")
    for marker in expected.get("log_contains", []):
        if marker not in log:
            errors.append(f"Missing player log marker {marker!r}")
    return errors


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def launch_with_capture(command, work, env, output, timeout):
    deadline = time.monotonic() + timeout
    errors = []
    with subprocess.Popen(command, cwd=work, env=env, stdout=output, stderr=subprocess.STDOUT) as player:
        try:
            while player.poll() is None and not (work / "scenario-native.json").is_file():
                if time.monotonic() >= deadline:
                    raise subprocess.TimeoutExpired(command, timeout)
                time.sleep(0.05)
            if player.poll() is None:
                capture = ROOT / "tools" / "capture-window.ps1"
                capture_command = ["powershell.exe", "-NoProfile", "-File", str(capture),
                                   "-ProcessId", str(player.pid), "-Output", "window.png"]
                try:
                    result = subprocess.run(capture_command, cwd=work, stdout=output, stderr=subprocess.STDOUT,
                                            timeout=min(10, max(0.01, deadline - time.monotonic())))
                    if result.returncode or not (work / "window.png").is_file():
                        errors.append("Window capture failed. See player.log")
                except subprocess.TimeoutExpired:
                    errors.append("Window capture exceeded its timeout")
                (work / "capture.done").touch()
            elif player.returncode == 0:
                errors.append("Player exited before the window could be captured")
            return player.wait(timeout=max(0.01, deadline - time.monotonic())), errors
        finally:
            if player.poll() is None:
                player.kill()


def run(args):
    scenario = read_json(args.scenario)
    script, polls = validate(scenario)
    player, rom = args.exe.resolve(strict=True), args.rom.resolve(strict=True)
    if file_hash(rom) != ROM_SHA256:
        raise ValueError(f"Expected the verified USA big-endian ROM at {rom}")
    player_hash = file_hash(player)
    number(args.timeout, 1, 900, "timeout")

    args.artifacts_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=scenario["name"] + "-", dir=args.artifacts_dir.resolve()))
    profile = work / "build/user-data"
    profile.mkdir(parents=True)
    write_json(work / "scenario.json", scenario)
    (work / "input.txt").write_text(script, encoding="ascii")
    for name, value in scenario.get("settings", {}).items():
        write_json(profile / name, value)
    managed = {"WDC_SCENARIO_INPUT": "input.txt", "WDC_RENDER_STATS": "1", "WDC_AUDIO_STATS": "1"}
    if args.capture:
        managed["WDC_SCENARIO_CAPTURE"] = "1"
    for field, key in (("warp", "WDC_WARP"), ("warp_at", "WDC_WARP_AT")):
        if field in scenario:
            managed[key] = scenario[field]
    # The fresh CWD isolates user data. Clear inherited WDC overrides so
    # unrelated checkpoints, captures and debug rates cannot affect this run.
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("WDC_")}
    env.update(managed)
    command = [str(player), str(rom)]
    report = dict(schema=1, command=command, player_sha256=player_hash, rom_sha256=ROM_SHA256,
                  environment=managed, timeout_seconds=args.timeout, result="failed", returncode=None)
    print(f"Scenario evidence in {work}", flush=True)
    errors = []
    log_path = work / "player.log"
    try:
        with log_path.open("wb") as output:
            if args.capture:
                code, capture_errors = launch_with_capture(command, work, env, output, args.timeout)
                errors.extend(capture_errors)
            else:
                code = subprocess.run(command, cwd=work, env=env, stdout=output,
                                      stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        report["returncode"] = code
        if code:
            errors.append(f"Player exited {code}. See player.log")
        else:
            native = read_json(work / "scenario-native.json")
            errors.extend(evaluate(scenario["expect"], native, polls, log_path.read_text(errors="replace")))
    except subprocess.TimeoutExpired:
        errors.append(f"Player exceeded the {args.timeout:g}s wall-clock timeout")
    except (OSError, ValueError) as error:
        errors.append(str(error))

    report["errors"] = errors
    if not errors:
        report["result"] = "passed"
    write_json(work / "scenario-result.json", report)
    for error in errors:
        print(error, file=sys.stderr)
    print(f"{scenario['name']}: {report['result']}")
    return int(bool(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", type=Path)
    executable = "worlddriverchampionship_modern" + (".exe" if os.name == "nt" else "")
    parser.add_argument("--exe", type=Path, default=ROOT / "build" / executable)
    parser.add_argument("--rom", type=Path, default=ROOT / ROM_NAME)
    parser.add_argument("--artifacts-dir", type=Path, default=ROOT / "artifacts" / "scenarios")
    parser.add_argument("--timeout", type=float, default=60, help="wall-clock backstop in seconds")
    parser.add_argument("--capture", action="store_true", help="capture the spawned Windows player window after replay")
    args = parser.parse_args()
    if args.capture and os.name != "nt":
        parser.error("--capture requires a Windows desktop")
    try:
        return run(args)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())

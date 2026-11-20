"""Check the ordinary player's saved interpolation setting across a race change."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("player", type=Path)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--rate", default="display", help="display or a whole number from 31 through 1000")
    parser.add_argument("--resolution", type=int, choices=range(9), default=0)
    args = parser.parse_args()
    rate = -1 if args.rate == "display" else int(args.rate)
    if rate != -1 and not 30 < rate <= 1000:
        parser.error("custom rate must be in 31..1000")
    player, rom = args.player.resolve(strict=True), args.rom.resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="wdc-display-"))
    config = work / "build/user-data"
    config.mkdir(parents=True)
    (config / "graphics.json").write_text(json.dumps(dict(
        version=2, rate=rate, resolution=args.resolution, interpolate_races=True,
        native_60fps=True,  # The saved interpolation choice selects fixed 30 Hz.
    )))
    env = dict(os.environ, WDC_WARP="1:0", WDC_WARP_AT="360:2:0:arcade:3", WDC_DIAGNOSTIC="1")
    for key in ("WDC_EXPERIMENTAL_DISPLAY_HZ", "WDC_STATE_FILE", "WDC_STATE_LOAD", "WDC_STATE_SAVE"):
        env.pop(key, None)
    log = work / "player.log"
    print(f"Player evidence in {log}", flush=True)
    with log.open("wb") as output:
        run = subprocess.run([str(player), str(rom)], cwd=work, env=env,
                             stdout=output, stderr=subprocess.STDOUT, timeout=90)
    data = log.read_text(errors="replace")
    if run.returncode != 2 or "diagnostic timeout: boot=1" not in data or "graphics fence unavailable" in data:
        raise SystemExit("Player failed its bounded diagnostic")
    tracks = data.split("[wdc:warp] loading track=")
    if len(tracks) != 3:
        raise SystemExit("Expected two race loads")
    for track, trace in enumerate(tracks[1:], 1):
        start = re.search(r"fixed 30 Hz gameplay, display target (\d+)/1 Hz", trace)
        if not trace.startswith(f"{track} ") or "race ready:" not in trace or not start:
            raise SystemExit(f"Track {track} did not enter the fixed-rate draw")
        hz = int(start[1])
        if rate != -1 and hz != rate:
            raise SystemExit(f"Player selected {hz} Hz for a {rate} Hz request")
        windows = re.findall(r"60 groups, ([\d.]+) s, (\d+) samples, RT64 accepted (\d+) explicit and (\d+) retrace frames", trace)
        if not windows:
            raise SystemExit(f"No complete observation window on track {track}")
        for seconds, samples, frames, retraces in windows:
            elapsed = float(seconds)
            if int(retraces) or not int(frames) or elapsed <= 0 or not 25 < int(samples) / elapsed < 35:
                raise SystemExit(f"Unexpected fixed-rate sample or presentation counts on track {track}: {windows}")
            print(f"Track {track}: {hz} Hz target, {int(frames)/elapsed:.1f} RT64 admissions/s, "
                  f"{int(samples)/elapsed:.1f} simulation samples/s")


if __name__ == "__main__":
    main()

"""Compare the same presentation frame and draw from two audit runs."""
import argparse
import json
from pathlib import Path
import struct
import zlib

from capture_present import read_capture


def find_capture(run, frame, draw):
    manifest = json.loads((run / "captures.json").read_text(encoding="utf-8"))
    matches = [entry for entry in manifest if (entry["frame"], entry["draw"]) == (frame, draw)]
    if len(matches) != 1:
        raise ValueError(f"{run}: expected one capture for frame {frame}, draw {draw}; found {len(matches)}")
    name = matches[0]["file"]
    if Path(name).name != name or Path(name).suffix != ".ppm":
        raise ValueError(f"{run}: invalid capture filename {name!r}")
    return run / name


def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def compare(baseline, candidate, crop, output):
    width, height, first = read_capture(baseline)
    candidate_width, candidate_height, second = read_capture(candidate)
    if (width, height) != (candidate_width, candidate_height):
        raise ValueError("Capture dimensions differ")
    x, y, crop_width, crop_height = crop
    if min(x, y) < 0 or min(crop_width, crop_height) < 1 or x + crop_width > width or y + crop_height > height:
        raise ValueError(f"Crop must fit within the {width}x{height} captures")
    if output.suffix.lower() != ".png":
        raise ValueError("Comparison output must be a .png")

    scanlines = bytearray()
    changed_pixels = max_channel_delta = 0
    for row in range(y, y + crop_height):
        start = (row * width + x) * 3
        end = start + crop_width * 3
        before, after = first[start:end], second[start:end]
        diff = bytes(abs(a - b) for a, b in zip(before, after))
        changed_pixels += sum(bool(r or g or b) for r, g, b in zip(diff[::3], diff[1::3], diff[2::3]))
        max_channel_delta = max(max_channel_delta, max(diff))
        scanlines.extend(b"\x00" + before + after + diff)  # PNG filter 0

    header = struct.pack(">IIBBBBB", crop_width * 3, crop_height, 8, 2, 0, 0, 0)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                       + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))
    return {"changed_pixels": changed_pixels, "max_channel_delta": max_channel_delta}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_run", type=Path)
    parser.add_argument("candidate_run", type=Path)
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--draw", type=int, default=0)
    parser.add_argument("--crop", type=int, nargs=4, metavar=("X", "Y", "WIDTH", "HEIGHT"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        first = find_capture(args.baseline_run, args.frame, args.draw)
        second = find_capture(args.candidate_run, args.frame, args.draw)
        stats = compare(first, second, args.crop, args.output)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(dict(frame=args.frame, draw=args.draw, crop=args.crop, **stats, output=str(args.output))))


if __name__ == "__main__":
    main()

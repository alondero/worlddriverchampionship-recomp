"""Measure X11 Present pixmap completions for a chosen player window on Linux."""
import argparse
from collections import Counter
import ctypes
import statistics
import struct
import time


def bind(library, name, result, *args):
    fn = getattr(library, name)
    fn.restype, fn.argtypes = result, args
    return fn


def observe(window, seconds, warmup):
    ptr, uint = ctypes.c_void_p, ctypes.c_uint32
    xcb = ctypes.CDLL("libxcb.so.1")
    present = ctypes.CDLL("libxcb-present.so.0")
    conn = bind(xcb, "xcb_connect", ptr, ctypes.c_char_p, ptr)(None, None)
    disconnect = bind(xcb, "xcb_disconnect", None, ptr)
    free = bind(ctypes.CDLL(None), "free", None, ptr)
    try:
        eid = bind(xcb, "xcb_generate_id", uint, ptr)(conn)
        # xcb_void_cookie_t contains a single unsigned sequence number.
        cookie = bind(present, "xcb_present_select_input_checked", uint, ptr, uint, uint, uint)(
            conn, eid, window, 2)  # XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY
        error = bind(xcb, "xcb_request_check", ptr, ptr, uint)(conn, cookie)
        if error:
            free(error)
            raise SystemExit("X server rejected the Present subscription for this window")
        poll = bind(xcb, "xcb_poll_for_event", ptr, ptr)
        start = time.monotonic()
        timestamps = []
        modes = Counter()
        while time.monotonic() - start < seconds + warmup:
            event = poll(conn)
            if not event:
                time.sleep(0.002)
                continue
            try:
                # xcb_present_complete_notify_event_t, native-endian wire fields.
                raw = ctypes.string_at(event, 32)
                if raw[0] & 127 != 35 or struct.unpack_from("=H", raw, 8)[0] != 1:
                    continue
                kind, mode, event_id, event_window = struct.unpack_from("=BBII", raw, 10)
                if (kind, event_id, event_window) != (0, eid, window) or time.monotonic() - start < warmup:
                    continue
                modes[mode] += 1
                if mode != 2:  # Skip reports do not display a pixmap.
                    timestamps.append(struct.unpack_from("=Q", raw, 24)[0])
            finally:
                free(event)
        return modes, timestamps
    finally:
        disconnect(conn)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("window", type=lambda s: int(s, 0), help="X11 window ID from xdotool or xwininfo")
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--warmup", type=float, default=2)
    args = parser.parse_args()
    if args.seconds <= 0 or args.warmup < 0:
        parser.error("duration must be positive and warmup nonnegative")
    modes, times = observe(args.window, args.seconds, args.warmup)
    if len(times) < 2:
        raise SystemExit("Too few Present pixmap completions to measure this window")
    intervals = sorted((b - a) / 1000 for a, b in zip(times, times[1:]))
    if intervals[0] < 0 or sum(intervals) <= 0:
        raise SystemExit("X server supplied invalid completion timestamps")
    print(f"{len(times)} completed pixmaps, {modes[2]} skipped, {1000/statistics.mean(intervals):.2f}/s, "
          f"median {statistics.median(intervals):.3f} ms, p95 {intervals[(95*len(intervals)-1)//100]:.3f} ms")


if __name__ == "__main__":
    main()

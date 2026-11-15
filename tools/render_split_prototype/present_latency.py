"""Join same-clock observations without assuming callback/worker finish order."""


def join_present_latency(bindings, swaps):
    if not bindings or [b["id"] for b in bindings] != [s["id"] for s in swaps]:
        raise ValueError("Presentation latency requires complete ordered swap bindings")
    rows = []
    for binding, swap in zip(bindings, swaps):
        screen, bound = binding["screen_us"], binding["bound_us"]
        dequeue, ready, begin, end = (swap[k] for k in ("dequeued_us", "ready_us", "begin_us", "us"))
        # A worker can dequeue or even swap before updateScreen returns to the
        # binding observer. Do not impose a false ordering across those threads.
        if not (0 <= screen <= bound and screen <= dequeue <= ready <= begin <= end):
            raise ValueError("Invalid presentation phase timestamps")
        rows.append({"id": binding["id"], "frame": binding["frame"], "draw": binding["draw"],
                     "screen_to_dequeue_ms": (dequeue - screen) / 1000,
                     "dequeue_to_ready_ms": (ready - dequeue) / 1000,
                     "ready_to_api_ms": (begin - ready) / 1000,
                     "swap_api_ms": (end - begin) / 1000,
                     "screen_to_swap_ms": (end - screen) / 1000,
                     "dequeued_before_bound": dequeue < bound})
    return rows

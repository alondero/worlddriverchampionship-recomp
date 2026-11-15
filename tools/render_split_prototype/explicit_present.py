"""Preserve RT64's ordinary VI handling while submitting requested extra frames."""


def patch_explicit_present(source):
    # Only the graphics worker's explicit prototype ScreenUpdateAction enables
    # this thread-local hook. Ordinary VI updates retain RT64's deduplication.
    # Leave VI history, framebuffer uploads, workload IDs and queue fences intact.
    seam = "        if (fromEarlyPresent || viDifferent || fbChangesMade || screenChangesMade) {"
    if source.count(seam) != 1:
        raise RuntimeError("RT64 explicit presentation seam changed")
    return ('extern "C" bool prototype_explicit_present();\n' + source.replace(
        seam, "        if (prototype_explicit_present() || fromEarlyPresent || viDifferent || fbChangesMade || screenChangesMade) {"))

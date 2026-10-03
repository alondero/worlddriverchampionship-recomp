# Testing

Tests should cross the smallest useful seam and use the strongest repeatable
evidence available.

| Evidence | Use |
| --- | --- |
| Host unit test | Pure policy, parsing, math, or classification |
| Synthetic RDRAM test | Guest-memory layout and byte-order contracts |
| ROM-backed test | Translated function or display-list boundary |
| Bounded end-to-end run | Startup, renderer, audio, input, or device path |
| Manual capture | Visual acceptance only, with exact repro details |

Run the source-only checks from the repository root:

```bash
python -B scripts/check_docs.py
python -m unittest discover tests
git diff --check
```

Once the ROM and generated image exist:

```bash
cmake --build build-player
ctest --test-dir build-player --output-on-failure
```

Headless runs must have a bounded frame/VI budget and a machine-readable result
when they are used in CI. A passing process exit alone is not proof that the
game reached the intended scene or submitted a graphics/audio task.

The first-playable headless diagnostic exits 2 on its 15-second limit and is
not a CI success gate. See [acceptance evidence](first-playable.md) for the
separately verified windowed practice-driving path. Synthetic tests cover
guest-memory math/string contracts and startup cache-marker transformation.
Audio tests use a synthetic circular buffer and an SDL device stub; they do
not require a physical audio endpoint. Graphics tests check that saving before
configuration-path initialization preserves live settings and leaves the
working directory's files untouched. They also include migration of
old forced-4:3 preferences to the camera-aware window aspect, and persistence
of an explicit Original choice in the new schema.
The RSP packed-load test runs the actual dependency helpers on synthetic DMEM,
covering all unsigned byte values, vector elements, byte alignment and wrapping.
The raw RDP clipping test uses a synthetic triangle and the vertex shader's
homogeneous GPU clipping rule to reproduce visible coverage loss from an
off-screen negative W. It verifies restored coverage and perspective texture,
depth and shade values, clips positive-W off-screen coverage, and preserves
fully visible and visible-singularity cases. Extreme finite inputs exercise
bounded polygon storage and attribute-overflow fallback.
An offset-aware standard four-sample MSAA fixture checks that conservative
clipping retains edge coverage at 1x, 2x and 3x, including fractional scissors.

`wdc_controller_pak` covers device reporting, raw-image byte order, block and
guest-buffer boundaries, ID protection, bank selection, failed publication,
and preservation of malformed files. `wdc_pfs_rom` is registered only when
the local USA ROM and generated CPU sources exist. It extracts the translated
SDK call closure into the build tree and exercises initialization/checking,
note allocation, write, reload with fresh guest RAM, read, free space and file
metadata. SI lock queues and the timer count are replaced in this
single-threaded harness; no Pak filesystem call is mocked. Missing translated
bodies fail generation.

`wdc_camera_rom` extracts the real camera/perspective/matrix call closure.
It loads ROM constants into word-swapped RDRAM and supplies a deterministic
camera and framebuffer descriptor. It checks wider horizontal projection and
visibility with unchanged vertical/depth terms, points newly entering the view,
16:9/ultrawide and narrow windows, and non-square-pixel framebuffer modes.
Zero/negative output dimensions are tested immediately after a wide camera,
checking that scale, projection and horizontal visibility return to 4:3.
CTest bounds this translated execution with a 30-second timeout.
Only display-list allocation is replaced in the projection closure. A separate
layout fixture retains the ROM's scene partition arithmetic and rectangle
writer while replacing mode switching, orientation setters and heap clearing.
It checks full-screen bounds and horizontal/vertical split boundaries.
Missing/indirect calls fail extraction.

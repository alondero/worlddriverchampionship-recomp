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
python -B scripts/check_repo.py
python -m unittest discover tests
git diff --check
```

The source-check workflow runs on every push and pull request. It also checks
that no ignored files are tracked and scans Git history for credentials with
[Gitleaks](https://github.com/gitleaks/gitleaks).

## ROM-free host tests

This mode builds the same twelve C++ host-test targets used by the player build,
without compiling the player, renderer, frontend or ROM translation. It uses
headers from pinned public dependencies and five of the normal dependency
patches. From a fresh checkout:

```bash
git submodule update --init --depth 1 lib/N64ModernRuntime lib/rt64
git -C lib/N64ModernRuntime submodule update --init --depth 1 N64Recomp
python3 -B scripts/patch_series.py --submodule lib/N64ModernRuntime \
    --patch patches/0008-runtime-controller-pak-kind.patch \
    --patch patches/0013-runtime-rsp-unsigned-packed-loads.patch \
    --patch patches/0018-runtime-rsp-dma-rounding.patch
python3 -B scripts/patch_series.py --submodule lib/rt64 \
    --patch patches/0014-rt64-rdp-screen-clipping.patch \
    --patch patches/0016-rt64-rdp-edge-trapezoids.patch
cmake -S . -B build-host-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DWDC_TESTS_ONLY=ON
cmake --build build-host-tests --parallel 2
ctest --test-dir build-host-tests --output-on-failure --no-tests=error
```

On Windows, use the native CMake and MinGW tools described in
[Building](../BUILDING.md), and set `Python3_EXECUTABLE` to the actual Python
executable if CMake finds a Windows Store alias. The patch helper restores the
paths a series owns before applying it, so it is safe to run against a tree a
player build already patched. `git apply --reverse --check` cannot verify that
state for patch 0014, because patch 0016 rewrites the context it matched.
`tests/test_patch_series.py` covers the helper on a synthetic repository.

## ROM-backed and player checks

Once the ROM and generated image exist:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

`wdc_startup_rom` checks that the translated input-worker startup initializes
every controller snapshot before making the worker runnable, including on
restart. See [startup ordering](reference/runtime.md#input-worker-startup-ordering).

With the frontend enabled, `wdc_input` exercises SDL virtual-gamepad hotplug,
native profile evaluation/remapping/persistence, analog axes and overlay input
gating without a ROM or GPU. `wdc_frontend_settings` checks staged host-owned
graphics/enhancement settings and the pending-open gate. Build these targets
explicitly if only frontend checks are needed:

```bash
cmake --build build --target wdc_input_tests wdc_frontend_settings_tests
ctest --test-dir build -R "wdc_(input|frontend_settings)$" --output-on-failure
```

See the [input reference](reference/input.md) for ownership and test limits.

`wdc_driving` checks pedal publication and guest-hook bounds with synthetic
RDRAM. `wdc_driving_rom` runs the translated input, gearbox and drivetrain
closure against USA ROM constants and controlled car parameters. It verifies
progressive throttle/brake torque, reverse and full-power digital overrides,
and rejects missing generated hooks. The frontend tests also cover Driving
defaults/persistence/reset and virtual-gamepad pedal release gating. These
checks do not validate physical pedal compatibility or racing feel.

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
The raw RDP edge test counts GPU sample centres left uncovered between two
neighbours at 1x, 2x, 4.8x and 8x. It uses a synthetic pair with 10-bit slopes
and a pair reduced from a captured Hawaii frame (rounded coefficients, no
command words); the three-point reconstruction fails both. It also checks
that exact coefficients keep the original four-sample MSAA coverage,
that attribute-plane offsets and interpolated shade match the plane, and that
every vertex lies on an RDP edge. It also checks that inverted spans and YM
outside YH..YL draw only what the edge walker does. GPU vertex snapping is not
modelled.
`tests/test_rdp_seams.py` covers the capture decoder and seam measurement in
`tools/rdp/` with the same synthetic encoding.

`wdc_controller_pak` covers device reporting, raw-image byte order, block and
guest-buffer boundaries, ID protection, bank selection, failed publication,
and preservation of malformed files. `wdc_pfs_rom` is registered only when
the local USA ROM and generated CPU sources exist. It extracts the translated
SDK call closure into the build tree and exercises initialization/checking,
note allocation, write, reload with fresh guest RAM, read, free space and file
metadata. SI lock queues and the timer count are replaced in this
single-threaded harness; no Pak filesystem call is mocked. Missing translated
bodies fail generation.

`wdc_rumble` checks motor identity, errors, no guest-memory access (including a
null-RDRAM call), filesystem preservation, absent ports and short lease. Its per-device output
fixture checks idle suppression, active refresh, exactly one stop and recovery
after a failed stop before driver caching. `wdc_rumble_rom` extracts the real
USA detection/request/timer and filesystem bodies and verifies pulse start/stop
while a note is allocated, written and reopened using the same primary Pak
object. A separate unpatched translation of the original motor initializer
compares its full 104-byte result against ROM-measured stores, using controlled
Controller Pak probe responses; virtual detection must leave the filesystem
object entirely unchanged. Reference code and recompiler inputs are generated
only in the build tree; production generated files are untouched. The effect
engine, gameplay predicate, callback setter and filesystem are translated.
The fixture compiles the pinned runtime's real controller-query source with
the port's device callback. `osContInit` populates the boot status; the worker
snapshot, query start/result and detector follow the pinned ROM call order.
The controller type is checked as guest bytes `00 05`. The first query has
no accessory transition, so detection must leave the motor uninitialized.
The timer's independent mode gate initializes before the first successful
effect, using the real predicate and startup callback. Callback selection,
later pause coverage and SI completion delivery are controlled.
The four initial mode/request gate words are asserted from the loaded ROM
image and again after the selected startup/query sequence, with no fixture
writes to those words before the first Init/Start. Later pause coverage writes
1 and then 0 to `8007D858`, asserting 1 before restoring 0. The input reference
inventories the observed values, ROM offsets, writers and deliberate controls.
No direct native Init or unused standalone helper enables that first effect.
The fixture also exercises the Pak menu's delete/count path with motor status
mask `0x08` clear. The full status/bank reader inventory is in the input reference.
`wdc_input` observes rumble through SDL's virtual
gamepad callback, including a transient stop failure, live strength, zero mute, settings/focus capture,
shutdown and detach. These are deterministic host/ROM checks, not a physical
controller playthrough. See [input](reference/input.md#rumble-alongside-saves).

`wdc_camera_rom` extracts the real camera/perspective/matrix call closure.
It loads ROM constants into word-swapped RDRAM and supplies a deterministic
camera and framebuffer descriptor. It checks wider horizontal projection and
visibility with unchanged vertical/depth terms, points newly entering the view,
16:9/ultrawide and narrow windows, and non-square-pixel framebuffer modes.
Zero/negative output dimensions are tested immediately after a wide camera,
checking that scale, projection and horizontal visibility return to 4:3.
CTest bounds this translated execution with a 30-second timeout.
Only display-list allocation is replaced in the projection closure. A separate
layout fixture retains the ROM's display initialization, descriptor lookup,
mode selection, scene partition arithmetic and rectangle writer. Framebuffer
allocation, orientation setters and heap clearing are outside that seam.
It checks full-screen bounds and horizontal/vertical split boundaries, plus
displayed projection proportions and horizontal culling in normal/Hi-Rez racing,
menu and cinematic modes at 4:3, 16:9, ultrawide and narrow output. Logical
picture dimensions are used independently of allocated buffer dimensions.
The test compiles the pinned RT64 VI decoder and extracts the production
presenter's coordinate transforms, substituting only GPU interfaces. Both VI
fields give 80 presentation cases. Reference dimensions are independent ROM/VI
measurements rather than fields read by the camera hook. The post-initializer
boot descriptor and seven malformed size pairs also exercise the real camera;
malformed dimensions preserve ROM aspect after a wide output and warn once.
The renderer reference records the decoded VI sizes, distinct render/presentation
aspects and mode 3's existing four-row sampling estimate.
Missing/indirect calls fail extraction.

`wdc_visibility_rom` exercises the translated car-detail selector, car
visibility decisions, track-detail buckets and bounded linked-section list.
It tests stock/enhanced thresholds, live restoration, preserved near/side and
section rejection, mode remapping, short loops and invalid extension links.
Packet allocation and non-decision setup are explicit boundaries; final BOSS
rendering is not mocked as a passing visual test. See
[distance and detail evidence](reference/visibility.md).

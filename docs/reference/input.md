# Input integration reference

The shared frontend owns device profiles, binding display and remapping. WDC
owns defaults, SDL-thread sampling, the gameplay snapshot, and the boundary
between settings input and guest input. The player instructions are in
[Controllers](../controllers.md).

## Peer comparison

[Zelda64Recomp's controls adapter](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/src/game/controls.cpp)
combines separate keyboard and controller mappings into N64 button bits and a
normalized stick. Its controller stick receives a deadzone before keyboard
directions are added; the result is clamped to the runtime range.
[Starfox64Recomp's adapter](https://github.com/sonicdcer/Starfox64Recomp/blob/f2996555d73ca480f3dc3639f375c50818847bfc/src/game/controls.cpp)
uses the same structure but scales the stick by 0.65. That scale is game policy
and is not evidence for WDC steering.

[AeroGauge's settings](https://github.com/alondero/aerogauge-recomp/blob/d4408065e88ab089f2c3e934370353e638f06e22/src/ui/aero_frontend_settings.cpp)
use `recompinput` defaults and game-specific action labels, then instantiate
the shared `create_controls_tab()`. This avoids maintaining another binding
editor. [Automobili Lamborghini's input adapter](https://github.com/alondero/automobililamborghini-recomp/blob/8936df4256932b19ce4e36be22b174da4ab8da27/src/ui/lambo_frontend_input.cpp)
also uses frontend profiles, while keeping analog pedals, steering changes
and phone sensors in the port. Those assists require separate game evidence.

## APIs in the pinned dependencies

These links describe WDC's pinned RecompFrontend revision, rather than assuming
that the newest upstream API is available.

- [Input mappings](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/input_mapping.cpp)
  provide per-device defaults, including signed controller half-axes. Set
  WDC defaults before loading profiles: setting defaults after their first use
  throws.
- [Profiles](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/profiles.cpp)
  load and save mappings and evaluate `get_n64_input()`. Loading initializes
  profiles and supplies defaults when the file is absent. Evaluation combines
  keyboard and controller input, applies the joystick deadzone, and clamps
  axes. The host must validate the N64 port: this function itself returns true
  without enforcing WDC's supported port count.
- [The controls tab](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/config/ui_config_tab_controls.cpp)
  constructs the shared controls page and saves `controls.json` beneath
  `recomp::get_config_path()` when the tab closes.
- [The controls page](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/config/ui_config_page_controls.cpp)
  reads action names, descriptions and bindings from the input library. Its
  binding buttons and clear/reset actions edit those same profiles.
- [Runtime input callbacks](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/ultramodern/include/ultramodern/input.hpp)
  expose polling, N64 state, rumble, and connected-device/accessory information.
  They do not implement an SDL device manager or binding editor.

## Host ownership requirements

[Frontend polling](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/input_state.cpp)
reads SDL keyboard and controller state. Keep sampling on WDC's SDL main
thread, and publish a coherent host snapshot for the runtime callback. Serialize
profile evaluation with the render-thread binding editor. The frontend's
capture check does not replace WDC's immediate pending-open gate or the
release-before-resuming rule.

[Frontend event handling](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/input_events.cpp)
combines hotplug, binding capture and menu events with fullscreen, launcher,
file-drop and quit behavior. Its public `handle_events()` consumes SDL's event
queue. A port retaining its own event loop must integrate this deliberately;
running both consumers loses events. The pinned remove path erases controller
state without closing the SDL handle, so the host must establish handle
ownership. Device-open failure must not proceed to GUID access.

Controller Pak support remains independent of button mappings. Preserve the
port's accessory callback; adding host rumble does not prove that the guest
Rumble Pak path works.

## WDC adapter and verification

`src/ui/input.cpp` configures defaults before `config::finalize()`, which loads
the native profiles. `src/main.cpp` owns the SDL queue and feeds events through
patch 0017's `process_event()`, then finishes the batch and samples profiles.
Fullscreen, quit and file-drop handling stay with WDC. The Controls tab is
the native editor, with WDC action names; General exposes its deadzone and
background-input and rumble-strength settings without advertising unsupported aiming.

The main thread and presentation callback acquire the same recursive mutex
before accessing native input/profile state. It precedes the frontend's UI
lock. Runtime input callbacks read a single atomic snapshot containing the
N64 button mask and signed normalized axes; they reject ports other than 1
and never call SDL. Opening the menu raises the existing capture gate before
presentation. Resuming requires every mapped gameplay input to return to
neutral, including opposing bindings whose combined stick value is zero.

## Analog driving bridge

`src/game_driving.cpp` adds three USA hooks from `rom-symbols.toml`:

| Boundary | Operand and behavior |
| --- | --- |
| `func_80002F58`, before `800031B4` | `a0` points to a controller ring record. Add accelerate/reverse/brake actions while preserving low menu/button bits. |
| `func_80016DF8`, before `80016EB4` | Replace `a3`'s hardcoded float32 1.0 with normalized throttle for `func_80017530`. |
| `func_80016DF8`, before `80017078` | Scale fresh front/rear brake torque in `f2`/`f0`, retaining front rolling resistance `min(20, full torque)`. |

The game owns eight 0x200-byte ring slots at `800B97E8`; each holds four
0x7C-byte port records. Record `+74` points to `OSContPad` at `800AAAE0 + port*6`.
The bridge validates slot, port and pointer agreement before accessing fields.
Record `+0` is a u32 action mask; `+25` is signed stick Y; `+27` disables driving
actions; `+28` records controller errors. Big-endian guest fields use the
word-swapped MEM helpers. The controller worker only adds high action bits:
`00010000` acceleration, `00020000` reverse below Y=-40, `00040000` braking.
Synthetic throttle/brake are tagged with bits 29/30. Physical A/B's low
translated bits (`0010`/`0020`) take precedence and keep full digital power.
Replay mode (`8007DF94 == 2`) ignores live pedals; replay/demo mask replacements
also clear the tags, so untagged driving stays on the original path.

SDL-thread sampling evaluates positive/negative half-axes through recompinput's
shared axis API. The pinned single-player mode leaves
`players::get_player(0).controller` unassigned; the shared API handles connected
controllers consistently with existing button bindings. The Driving config is
read under the input mutex and values are bounded before normalization. One
lock-free atomic pair per port publishes unitless float32 values in [0,1]; only
port 1 currently receives host input. Guest hooks never call SDL or retain guest
pointers. The pending-open gate immediately masks pedal reads. Sampling also
publishes zero while the overlay is open or waiting for all gameplay inputs,
including enabled pedals below the digital button threshold, to release.
Replace the address bridges with named input/drivetrain APIs when decompiled.

The `wdc_input` CTest target uses SDL's virtual gamepad API with the real
native profiles/event/polling code. It covers hotplug, failed opens, partial
steering and Y direction, face/C/D-pad inputs, capture and release gating,
binding cancellation/remapping, saved-profile reload, a 100% deadzone,
disconnection and absent guest ports. It requires frontend dependencies but
no ROM, physical controller or GPU. This fixture does not establish physical
device compatibility or validate racing feel.

`wdc_input` also covers opt-in pedals, positive/negative half-axes, None,
deadzones/saturation, simultaneous pedals, held-trigger release gating and
detachment. `wdc_frontend_settings` checks Driving defaults, native persistence
and live reset. `wdc_driving` uses synthetic RDRAM for all ring slots/ports,
pointer bounds, disabled/error records, invalid host values, replay and digital
overrides. `wdc_driving_rom` extracts the real input/drivetrain call closure,
including gearbox, steering and torque calculations, with only the port hooks
native. Controlled car parameters and USA ROM constants verify progressive
drive/brake torque, reverse requests and digital overrides. Missing translated
bodies, indirect calls or generated pedal hooks fail extraction. CTest bounds
the ROM fixture to 30 seconds; it is not a physical-controller playthrough.

## Rumble alongside saves

The accessory callback still reports `ControllerPak`. WDC uses distinct
104-byte `OSPfs` objects: the save/menu probe starts at `800AAAF8`, while the
motor worker uses `800AAC98` (port 2 would be `800AAD00`). These were measured
from the USA calls at `8003BB24`, `8004D4DC`, `8004D904` and `8004DCEC`.
The native motor adapter accepts only the port-1 motor object, returning
`PFS_ERR_DEVICE` for the filesystem probe and `CONTRFAIL` for absent channels.
No filesystem structure or Pak image is changed by a motor call. Initialization
is host-owned; Start/Stop validate the object identity without dereferencing it.
This deliberately bypasses the runtime's exclusive accessory-kind check.

`func_8004D9B0` still creates the original effects; `func_8004D450` retains
their pulse timers, enable checks and game-mode gates. The hook before
`8004D760` renews a 150 ms host lease only for player 1. On the input worker,
it reads the game's u16 effect-pending and motor-on fields at `800DF464` and
`800DF468` with big-endian, word-swapped helpers; both are fixed within 8 MiB
RDRAM. Finished/reset effects cancel the lease. No guest pointer escapes the
call. Replace these addresses and the object identity with named effect and
accessory interfaces when decompiled.

The SDL main thread samples the atomic lease under the existing input mutex,
reads General's saved strength, and borrows the frontend's open handles. It
uses the high-frequency motor, as the shared frontend does, with 150 ms SDL
commands. It does not open additional handles. Unsupported devices are skipped
after their first failure. Settings capture, release gating, zero strength,
focus loss, hotplug and normal exit clear feedback; a stalled guest or host
cannot leave an indefinite motor command. Headless/keyboard-only builds have
no physical sink. No dependency patch or change to save-device reporting is
needed.

`wdc_rumble` covers motor identity, errors, no guest-memory mutation, absent
ports, lease expiry and cancellation. `wdc_rumble_rom` runs the real detection,
request and effect-engine bodies with a controlled gameplay predicate, and
writes/reloads a Pak block during a live effect. `wdc_input` checks actual SDL
virtual-gamepad output, strength, zero mute, settings/focus gating, shutdown and
detach. `wdc_frontend_settings` checks strength persistence and default reset.
These checks do not establish physical gamepad compatibility or racing feel.

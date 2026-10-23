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
background-input settings without advertising unsupported rumble or aiming.

The main thread and presentation callback acquire the same recursive mutex
before accessing native input/profile state. It precedes the frontend's UI
lock. Runtime input callbacks read a single atomic snapshot containing the
N64 button mask and signed normalized axes; they reject ports other than 1
and never call SDL. Opening the menu raises the existing capture gate before
presentation. Resuming requires every mapped gameplay input to return to
neutral, including opposing bindings whose combined stick value is zero.

The `wdc_input` CTest target uses SDL's virtual gamepad API with the real
native profiles/event/polling code. It covers hotplug, failed opens, partial
steering and Y direction, face/C/D-pad inputs, capture and release gating,
binding cancellation/remapping, saved-profile reload, a 100% deadzone,
disconnection and absent guest ports. It requires frontend dependencies but
no ROM, physical controller or GPU. This fixture does not establish physical
device compatibility or validate racing feel.

# Controllers

The default player supports SDL gamepads and keyboard together on controller
port 1. Connect a gamepad before starting or plug it in while playing. In this
single-player mode, connected gamepads share port 1; other ports are absent.

| N64 input | Keyboard default | Gamepad default (Xbox labels) |
| --- | --- | --- |
| A / accelerate / select | X | A (south) |
| B / brake / back | Z | B (east) |
| Start / pause | Enter | Start / Menu |
| Stick / steering | Arrows or WASD | Left stick |
| Z trigger | Space | Left trigger |
| R | Right Shift | Right trigger |
| L | Left Shift | Left shoulder |
| C buttons | I / K / J / L | Right stick; Y = C Left, right-stick click = C Up, right shoulder = C Down |
| D-pad | T / G / F / H | D-pad |
| Settings | Escape or F1 | Back / View |

Open **Settings > Controls** to view or rebind the actual mappings. The
controller/keyboard toggle at the bottom switches the device being edited.
Choose a binding and press a key, button or move an axis. Escape or the mapped
controller Settings button cancels capture. Each input has two binding slots;
clear individual inputs or use **Reset to defaults** for the selected device.
Bindings take effect immediately and save when leaving the Controls tab.

Navigate settings with the mouse, arrows/Tab and Enter, or the gamepad's
D-pad/left stick and A. B goes back, Y/Start applies staged graphics changes,
and the shoulders switch tabs. Controller menu actions can also be rebound.
F11 or Alt+Enter toggles fullscreen.

**General** provides rumble strength (25% by default), joystick deadzone (5%) and background controller
input. Changes take effect immediately and save when leaving the General tab.
Analog steering retains partial stick
deflection; keyboard directions use full deflection. Set **Rumble Strength** to
zero to disable vibration. The game's original effects drive compatible SDL
gamepads while a Controller Pak remains attached to port 1 for saves. As with
single-player input, all connected gamepads receive feedback. Opening settings,
losing window focus or unplugging a controller clears pending vibration.
Rumble requires the frontend-enabled player; keyboard-only builds have no
physical controller output. Physical-controller feel still needs validation.

**Driving** adds optional analog throttle and brake. Set each **input mode** to
**Analog** to enable it; both default to Digital. Throttle defaults to the right
trigger (RT), brake to the left trigger (LT). You can choose any SDL gamepad
stick/trigger axis or None, and select the negative half of a stick axis.
Deadzone ignores the first 5% by default; saturation sets the axis position
that reaches full power (100% by default). These settings are independent of
the steering deadzone. Changes take effect immediately and save when leaving
Driving; **Restore defaults** resets both pedals to Digital.

A and B bindings still give full throttle/braking and select/back in menus.
Analog pedals add driving actions without acting as menu A/B presses. Pull the
steering stick down while accelerating to request reverse, as with digital A.
LT/RT retain their Z/R bindings in Controls; clear conflicting bindings there
if you want those axes to operate only as pedals. Pedals require an SDL
gamepad axis; a wheel or pedal device needs a suitable SDL gamepad mapping.

Opening settings blocks gameplay input immediately. Closing keeps input neutral
until the mapped gameplay buttons, stick and enabled pedals return to neutral. **Pause with
Start before opening settings if you want to stop a race**: settings do not
pause the game. Use the overlay's quit button or close the window to exit.

Bindings and general preferences use RecompFrontend's native storage under
`build/user-data/controls.json`, `general.json` and `driving-controls.json`. See
[configuration](configuration.md) for the independent Controller Pak save.
Direct builds with `WDC_ENABLE_FRONTEND=OFF` retain the fixed keyboard-only
adapter. The desktop build scripts enable the frontend by default.

Developer ownership, peer comparisons and test limits are recorded in the
[input reference](reference/input.md).

## Optional generic N64 USB profile (Windows)

For the measured generic Chinese N64 USB layout below, download the
[optional SDL mapping](controller-profiles/generic-n64-windows.txt), then launch
from PowerShell in the game's directory (adjust the two relative paths):

```powershell
$previousMapping = $env:SDL_GAMECONTROLLERCONFIG_FILE
try {
    $env:SDL_GAMECONTROLLERCONFIG_FILE = (Resolve-Path './generic-n64-windows.txt').Path
    & './worlddriverchampionship_modern.exe'
} finally {
    $env:SDL_GAMECONTROLLERCONFIG_FILE = $previousMapping
}
```

Use the actual executable filename in your release. This affects the launched
process and restores the shell's previous setting afterward. Nothing is installed
or added to a global controller database. Close the game before applying it.

Use the default controller bindings in **Settings > Controls**. This profile
maps physical B to SDL B and physical R to SDL right trigger, matching WDC's
defaults. Other ports' N64 presets may instead map B to SDL X or R to right
shoulder, which would select different actions here. Keep **Driving** throttle
and brake in their default **Digital** modes: this unit has button triggers,
not analog pedals. If desired, try **General > Joystick Deadzone = 2%** as a
starting point from the unit's center measurements, then check steering; the
SDL mapping itself does not set a deadzone.

| Physical control | Raw SDL input (zero-based) | Profile output |
| --- | --- | --- |
| A / B | buttons 5 / 4 | A / B |
| Start | button 9 | Start |
| L / R / Z | buttons 6 / 7 / 8 | Left shoulder / right trigger / left trigger |
| C up / right / down / left | buttons 0 / 1 / 2 / 3 | Right stick up / right / down / left |
| Stick X / Y | axes 0 / 1 | Left stick X / Y |
| D-pad | hat 0 | D-pad |

There is no measured Select/Back button. Open settings with **Escape or F1**;
Z remains the N64 Z trigger. Buttons 10 and 11 are unconfirmed and unbound.
Use keyboard/mouse for settings actions without a corresponding physical button.

Only opt in if your device has this layout. The measured unit reports USB
VID/PID `0079:0006`, four axes, twelve declared buttons and one hat. The file
contains the observed Windows CRC variants `8630` (SDL 2.32.10), `8e4f`
(SDL 2.26.3) and `79be` (SDL 3.4.18). SDL2 called it `USB Joystick`; SDL3
called it `Microntek USB Joystick`. Matching IDs or CRCs do **not** identify a
layout uniquely, and older SDL versions can fall back to a broader match.
Incompatible clones can share them; check every physical control before racing.

The raw assignments were provided by MatyMeatBoy from a controller measurement
record, with permission to reuse them with attribution: eighteen controls,
206 samples over 10.005 seconds. Native SDL virtual-device checks verify the mapping translation;
physical gameplay in WDC has not been verified. If controls differ, remove the
environment setting and use the normal binding editor rather than applying this
profile to an unidentified clone.

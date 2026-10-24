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

**General** provides joystick deadzone (5% by default) and background controller
input. Changes take effect immediately and save when leaving the General tab.
Analog steering retains partial stick
deflection; keyboard directions use full deflection. Rumble is not implemented;
a Controller Pak remains attached to port 1 for game saves.

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

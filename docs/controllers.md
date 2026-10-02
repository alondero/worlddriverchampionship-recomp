# Controllers

The first Windows player exposes controller port 1 through the keyboard:

- Enter: Start/pause; X: A/accelerate/select; Z: B/brake/back.
- Arrows or WASD: analog stick, with full deflection in each direction.
- Space: Z trigger; right Shift: R; Escape or F1: open/close the settings overlay.
- Use the overlay's quit button or close the window to exit.
- Gamepad, C buttons, rebinding, and rumble: not implemented.

A Controller Pak is attached to port 1. The game's save/load and Pak manager
operate on the persistent image described in [configuration](configuration.md).
No accessory is attached to other ports.

SDL is sampled on the main thread. Atomic button/stick snapshots feed runtime
controller reads; the runtime converts normalized axes to the N64 stick range.
Opening the overlay blocks gameplay input immediately. Closing it keeps input
neutral until held gameplay keys are released. Pause with Enter before opening
settings if you want to stop a race; opening settings does not pause the game.
Hawaii practice acceptance covered acceleration, steering, and pause/resume.
See [first-playable evidence](first-playable.md) for the exact menu route.

Do not copy another game's control assumptions. Keep SDL/controller-profile
handling in the host module and the game's semantic actions in a small adapter.

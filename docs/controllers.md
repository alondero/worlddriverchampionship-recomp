# Controllers

The first Windows player exposes controller port 1 through the keyboard:

- Enter: Start/pause; X: A/accelerate/select; Z: B/brake/back.
- Arrows or WASD: analog stick, with full deflection in each direction.
- Space: Z trigger; right Shift: R; Escape: quit the host.
- Gamepad, C buttons, rebinding, rumble, and Controller Pak saves: not implemented.

SDL is sampled on the main thread. Atomic button/stick snapshots feed runtime
controller reads; the runtime converts normalized axes to the N64 stick range.
Hawaii practice acceptance covered acceleration, steering, and pause/resume.
See [first-playable evidence](first-playable.md) for the exact menu route.

Do not copy another game's control assumptions. Keep SDL/controller-profile
handling in the host module and the game's semantic actions in a small adapter.

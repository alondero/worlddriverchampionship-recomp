# World Driver Championship: Recompiled

Experimental native Windows build of the Nintendo 64 game using N64Recomp,
N64ModernRuntime, and RT64. CPU code and the custom BOSS graphics microcode
are statically recompiled; RT64 renders the resulting low-level RDP commands.

## First playable

Windows x64 / Direct3D 12 has been tested through the intro, menus, and
Hawaii practice driving (acceleration, steering, automatic gears, pause/resume).
This is an early development build, not a finished port.

Supply your own USA ROM, then run:

```powershell
./build.ps1
./play.ps1
```

Gamepads are supported: left stick steers, A accelerates, B brakes, Start pauses,
and Back/View opens settings (Xbox button labels). Keyboard defaults remain
Enter for Start/pause, X for A/accelerate, Z for B/brake, and arrows/WASD to steer.
Use Settings > Controls to rebind either device; see [Controllers](docs/controllers.md).
For the tested path: skip intro with Enter, press Enter at the title, then
X for Quick Race, Down and X for Practice Laps, X for Hawaii, and X for automatic transmission.

Controller Pak saves are enabled on port 1 and persist in
`build/user-data/saves/worlddriverchampionship.us.mpk`. See
[save configuration](docs/configuration.md) for backup and failure behavior.

Known limitations include rendering glitches and no rumble. SDL audio output is experimental. Linux and Android
are not validated. No ROM, generated game code, or binary is committed.

Graphics: automatic or 1-8x resolution scaling, MSAA, output filtering, saved
settings, and fullscreen (F11 / Alt+Enter). Press Escape or F1 for the tabbed
settings overlay, with Apply/Discard, Restore Defaults and editable controls.
Race presentation widens the camera to match the window, preserves car/HUD
proportions and removes the outer TV borders. Original 4:3 remains available.
Experimental presentation-rate controls are available; interpolated motion
remains future work. See [Graphics settings](docs/configuration.md) and the
[peer capability review](docs/graphics-capabilities.md).

See [Building](BUILDING.md), [First-playable evidence](docs/first-playable.md),
[Testing](docs/testing.md), and [Peer research](docs/research/first-playable.md).

## License and contents

Project source is provided under [GPL version 3](LICENSE). Dependency and font
licenses remain with their respective components; the overlay's font notices
are included in [assets/frontend](assets/frontend/README.md).
The original game and its assets are not included. This is an unofficial project.

Maintainers: see [publishing checks](docs/publishing.md) before changing visibility
or distributing build artifacts.

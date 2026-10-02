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

Enter is Start/pause, X is A/accelerate, Z is B/brake, and arrows/WASD steer.
For the tested path: skip intro with Enter, press Enter at the title, then
X for Quick Race, Down and X for Practice Laps, X for Hawaii, and X for automatic transmission.

Controller Pak saves are enabled on port 1 and persist in
`build/user-data/saves/worlddriverchampionship.us.mpk`. See
[save configuration](docs/configuration.md) for backup and failure behavior.

Known limitations: keyboard only,
and rendering glitches. SDL audio output is experimental. Linux and Android
are not validated. No ROM, generated game code, or binary is committed.

Graphics: automatic or 1-8x resolution scaling, MSAA, output filtering, saved
settings, and fullscreen (F11 / Alt+Enter). Use the Windows Graphics menu or
F1 for shortcuts. Experimental aspect and presentation-rate controls are also
available; true wider-camera rendering and interpolated motion remain future
work. See [Graphics settings](docs/configuration.md) and the
[peer capability review](docs/graphics-capabilities.md).

See [Building](BUILDING.md), [First-playable evidence](docs/first-playable.md),
[Testing](docs/testing.md), and [Peer research](docs/research/first-playable.md).

# PC stage viewer

The first native executable is a small SDL3 viewer for the extracted Neo South
Island Act 1 data. It is not the game engine yet; it is a ROM-free inspection
tool that proves the native PC shell can display the reconstructed stage data
through the original 160 by 152 viewport.

## Build

First generate the local level data:

```powershell
py -3 tools\extract_level.py
```

Then build the viewer:

```powershell
.\scripts\build-viewer.ps1 -Configuration Release
```

The script configures CMake, fetches the pinned SDL3 source release, builds the
viewer, and copies `SDL3.dll` beside the executable. Build output stays under
the ignored `build/` directory.

Run it with:

```powershell
.\build\viewer\Release\sonic-pocket-viewer.exe .\out\nsi1
```

Or build and run in one step:

```powershell
.\scripts\build-viewer.ps1 -Configuration Release -Run
```

## Controls

- Left/right arrows or A/D: move Sonic
- Space or Z: jump
- C: toggle the collision overlay
- R: reset Sonic to the original stage start position
- 1 through 6: set exact integer window scale
- Escape: quit

Gamepads are supported too:

- Left stick or D-pad: move Sonic
- South face button: jump
- East face button: toggle collision
- Back button: reset Sonic

The viewer renders Sonic animation frames from the user's ROM and drives a
first native movement prototype over a byte-per-pixel collision mask generated
from the level's Plane 2 collision data. It is intentionally simple: no rolling,
springs, enemies, object interaction, full animation script interpreter, or
exact player physics yet.

Camera movement uses the original stage bounds: X starts at 64 and ends 224
pixels before the layout edge; Y starts at 64 and ends 216 pixels before the
layout edge. The extracted map includes the game's 64-pixel guard band, so the
viewer samples `camera + 64` from the PNG while keeping the displayed camera
coordinates in original game-space units. Recenter therefore places the start
marker at the game's mapped 48-pixel horizontal follow target.

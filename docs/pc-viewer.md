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

- Arrow keys or WASD: move the camera
- Shift: faster camera movement
- C: toggle the collision overlay
- R: recenter on Sonic's original stage start position
- 1 through 6: set exact integer window scale
- Escape: quit

Gamepads are supported too:

- Left stick or D-pad: move the camera
- South face button: toggle collision
- Back button: recenter

The blue/yellow cross marks the original player start coordinate from the level
metadata. The marker is temporary; Sonic's real sprite and update loop come in a
later native-engine milestone.

Camera movement uses the original stage bounds: X starts at 64 and ends 224
pixels before the layout edge; Y starts at 64 and ends 216 pixels before the
layout edge. Recenter therefore places the start marker at the game's mapped
48-pixel horizontal follow target.

# Sonic Pocket

A reverse-engineered native PC reimplementation of *Sonic the Hedgehog: Pocket
Adventure*.

## Status

The reference ROM, startup, VBlank, controller polling, cooperative task
scheduler, front-end flow, and first gameplay task chain are mapped. Sonic's
player task at RAM `0x6708` now has confirmed position, velocity, collision
bounds, crouch, spindash, roll, and airborne fields and states. A BizHawk trace
also confirms a 30 Hz player gameplay tick over 60 Hz presentation.

The current milestone is to finish the player state machine and collision
model, then translate one representative stage into native code.

See the [project roadmap](roadmap.md) for phases, progress, and exit criteria.

## Quick start

Place a legally obtained ROM in the local `Rom/` directory, then run:

```powershell
python tools/rominfo.py
python tools/extract_level.py
.\scripts\build-viewer.ps1 -Configuration Release -Run
python -m unittest discover -s tests -v
```

The verification tool accepts only the known reference image described in
[`config/rom.json`](config/rom.json). See
[`docs/getting-started.md`](docs/getting-started.md) for the analysis workflow.
The level extractor currently exports Neo South Island Act 1 as inspectable
plane/collision PNGs, object JSON, and raw data under the ignored `out/nsi1/`
directory.
The native SDL3 prototype then displays that data through a 160 by 152
integer-scaled viewport with controllable Sonic movement; see
[`docs/pc-viewer.md`](docs/pc-viewer.md).

### Title-screen parity workflow

The title screen has a local teacher-capture fallback so the viewer can play
the measured ROM title sequence while the ROM-derived extractor is still being
completed. Capture frames with BizHawk, then import them locally:

```powershell
python tools/extract_title.py --all-variants
python tools/import_title_teacher_sequence.py --start 460
build\viewer\Release\sonic-pocket-viewer.exe --title-screen
```

Captured PNGs stay under ignored `out/title-teacher/` and `out/title/`
directories and must not be committed. Generated extractor frames should only
replace teacher frames after `tools/compare_title_frames.py` proves they match
the BizHawk reference.

When generated title frames diverge from the ROM, capture the live title object
state too:

```powershell
# In BizHawk's Lua Console, run:
scripts\bizhawk-title-object-trace.lua

# Then summarize the newest trace:
python tools/analyze_title_object_trace.py --start 560 --end 660
```

The object trace records the runtime `SprObjRAM` list plus copied sprite tile,
position, and palette data after `CopySprites`. Use it to port the title-state
transition instead of tuning sprite placement by eye.

## Repository policy

This repository does not contain the original ROM, extracted game assets,
Neo Geo Pocket Color BIOS, emulator installations, or generated full-ROM
disassemblies. Contributors must provide their own legally obtained game data.

## Porting policy

This project is a native port/reimplementation, not a level-specific remake.
Gameplay fixes should be made by porting shared ROM routines and validating
them against traces, disassembly, and analysis tools. Do not fix individual
ramps, slopes, objects, or screens with one-off coordinate hacks unless the ROM
itself contains an equivalent special case.

Preferred evidence sources:

- the checked-in human notes under `analysis/` and `docs/`;
- the local SPA disassembly/tools bundle, if present at
  `SonicPocketAdventure_disasm+tools/`;
- Ghidra with the TLCS-900/H processor and Neo Geo Pocket loader;
- BizHawk runtime traces from `scripts/bizhawk-player-trace.lua`;
- native replay output from the viewer's `--replay-trace` mode.
- title-screen teacher captures imported by
  `tools/import_title_teacher_sequence.py`, for visual parity while replacing
  captured frames with generated ROM/disassembly-derived frames.

When behavior differs from the ROM, identify the original routine first, port
the general rule, then prove the change with a trace or focused runtime test.

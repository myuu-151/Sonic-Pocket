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
python -m unittest discover -s tests -v
```

The verification tool accepts only the known reference image described in
[`config/rom.json`](config/rom.json). See
[`docs/getting-started.md`](docs/getting-started.md) for the analysis workflow.
The level extractor currently exports Neo South Island Act 1 as inspectable
plane/collision PNGs, object JSON, and raw data under the ignored `out/nsi1/`
directory.

## Repository policy

This repository does not contain the original ROM, extracted game assets,
Neo Geo Pocket Color BIOS, emulator installations, or generated full-ROM
disassemblies. Contributors must provide their own legally obtained game data.

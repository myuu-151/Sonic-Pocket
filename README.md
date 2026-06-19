# Sonic Pocket

A reverse-engineered native PC reimplementation of *Sonic the Hedgehog: Pocket
Adventure*.

## Status

The reference ROM, startup, VBlank, controller polling, cooperative task
scheduler, front-end flow, and first gameplay task chain are mapped. Sonic's
player task at RAM `0x6708` now has confirmed position and velocity fields.

The current milestone is to finish the player state machine and collision
model, then translate one representative stage into native code.

See the [project roadmap](roadmap.md) for phases, progress, and exit criteria.

## Quick start

Place a legally obtained ROM in the local `Rom/` directory, then run:

```powershell
python tools/rominfo.py
python -m unittest discover -s tests -v
```

The verification tool accepts only the known reference image described in
[`config/rom.json`](config/rom.json). See
[`docs/getting-started.md`](docs/getting-started.md) for the analysis workflow.

## Repository policy

This repository does not contain the original ROM, extracted game assets,
Neo Geo Pocket Color BIOS, emulator installations, or generated full-ROM
disassemblies. Contributors must provide their own legally obtained game data.

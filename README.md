# Sonic Pocket

A reverse-engineered native PC reimplementation of *Sonic the Hedgehog: Pocket
Adventure*.

## Status

Early analysis. The reference ROM is identified, its cartridge header is
understood, and the TLCS-900/H entry point at `0x200040` has been decoded.

The current milestone is to map startup, interrupts, the frame loop, and the
major engine subsystems before translating gameplay code.

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

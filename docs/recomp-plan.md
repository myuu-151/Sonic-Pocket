# Static recompilation plan

This branch replaces the hand-written port (`src/viewer`) with a static
recompilation of the cartridge's TLCS-900/H code. Game logic is translated
mechanically into C++, so it behaves like the original by construction instead
of being tuned against traces.

## Why the first attempt stalled

- Game logic was rewritten by hand and fitted to BizHawk traces. Each routine
  was an approximation, so there was always another mismatch (for example the
  Neo South Island ramp landing angle `0x13` vs `0x0F`).
- The native data model did not match the ROM's RAM layout (a Y-flipped
  collision mask, boolean state instead of state function pointers), so every
  ported routine needed glue code.
- The title screen became playback of pre-rendered frames rather than code.
- ValleyBell's complete, reassemblable disassembly was only used as reference
  material, not as input.

## Architecture

```
ROM ──► recompiler ──► generated C++ (one function per routine)
                              │
                              ▼
          runtime: NGPC machine (memory, timers, interrupts, K2GE video,
                   Z80 + T6W28 sound, flash, HLE BIOS) + SDL3 frontend
```

1. **Machine** (`src/ngpc/`). A Neo Geo Pocket Color built from MAME's
   BSD-3-licensed TLCS-900/H core (`src/cpu/tlcs900/`), TMP95C061 peripherals
   and K1GE/K2GE video, plus a high-level BIOS whose boot state and interrupt
   dispatch match the Mednafen/BizHawk core. With the interpreter as the CPU,
   this is a complete emulator and the baseline everything else is checked
   against.
2. **Recompiler**. Decodes the ROM using the same opcode tables as the
   interpreter and emits C++ that calls the interpreter's own ALU and flag
   helpers. Every block adds its cycle count and checks for pending
   interrupts, so video raster effects and timer interrupts behave as they do
   in the interpreter. Indirect jumps and calls go through an
   address-to-function table; anything unknown (for example the flash routine
   the game copies to RAM at `0x6E00`) falls back to the interpreter.
3. **Frontend**. SDL3 window, input and audio on top of the machine.

## Verification

- **Machine vs BizHawk.** `scripts/bizhawk-dump-reference.lua` records work RAM
  every frame; `ngpc-run --dump-ram` records ours;
  `tools/compare_ram_frames.py` compares them. Current state: after boot, 1078
  of 1198 frames are byte-identical outside the stack. The remaining
  differences are the SEGA voice sample pointer (one sample of phase), boot
  frames and a short run of object bytes around frame 600.
- **Recompiled vs interpreter.** Both run on the same machine, so they must
  agree exactly. `spa-verify` runs both side by side and compares registers,
  cycle count, work RAM, sound RAM, I/O and video RAM after every frame.
  Current state: 1200 frames in exact lockstep. The only code still
  interpreted is the flash routine the game copies to RAM (`0x6E00`) and the
  HLE BIOS entry points.

## Building

```powershell
cmake -S . -B build/recomp -G "Visual Studio 18 2026" -A x64 `
      -DSPA_ROM="<path to cartridge>"
cmake --build build/recomp --config Release --target spa-verify -- /m:1
```

`config/recomp/functions.txt` lists routine entry points. It is regenerated
from an assembled listing of the disassembly with `tools/recomp_seeds.py`,
which also writes the instruction list that `spa-recomp --check-listing`
uses to validate the decoder (all 49,933 instructions decode identically).
The generated code is large; build it with one compiler process on low-power
machines.

## Known deliberate differences from BizHawk

- `0x6C58` (flash chip size) is 3 for a 16 Mbit cartridge, as on hardware.
  Mednafen stores 1, which makes the game's erase routine target a code block;
  Mednafen's flash model ignores that erase and ours does not.
- Timer prescaler rates follow Mednafen (T1 = 256 cycles), not MAME's newer
  databook rates, because they match the reference captures.

## Repository policy

Generated code is derived from the ROM, so it is produced at build time from
the user's own cartridge and never committed. Addresses and function lists
(facts about the ROM) may be committed.

# Static recompilation plan

This project replaces an earlier hand-written port with a static
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
2. **Recompiler** (`src/recomp/`, runtime in `src/recomp_rt/`). Decodes the
   ROM with the interpreter's own opcode tables and emits one C++ function per
   routine. Each instruction does the interpreter's operand setup and calls
   its handler, so semantics are identical by construction; jumps, calls and
   returns become gotos, C++ calls and returns. Every instruction runs the
   same interrupt and timer bookkeeping as one interpreter step, so raster
   effects and interrupts land exactly where they do in the interpreter.
   Indirect jumps and calls go through an address-to-function table; anything
   unknown (for example the flash routine the game copies to RAM at `0x6E00`)
   falls back to the interpreter, and stack reloads (`KillObject`) unwind to a
   top-level trampoline.
3. **Frontend**. SDL3 window, input and audio on top of the machine.

## Verification

- **Machine vs BizHawk.** `scripts/bizhawk-dump-reference.lua` records work RAM
  every frame; `ngpc-run --dump-ram` records ours;
  `tools/compare_ram_frames.py` compares them. Current state: after boot, 1077
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
- The Z80 is interrupted on every timer 3 match, as in Mednafen. MAME only
  interrupts on rising edges of timer flip-flop 3, which plays the music at
  half tempo.

## Repository policy

Generated code is derived from the ROM, so it is produced at build time from
the user's own cartridge and never committed. Addresses and function lists
(facts about the ROM) may be committed.

## Roadmap

1. **Readable output.** Lift the generated code to plain C++ expressions and
   structured control flow instead of handler calls, with timing bookkeeping
   per block rather than per instruction. Faster and far easier to read.
2. **Gameplay coverage.** Record input while playing through every zone,
   boss, special stage and menu, and run the lockstep check against those
   recordings instead of only the attract sequence.
3. **Typed RAM.** Describe the known RAM layouts (task records, the player at
   `0x6708`, sprite lists, level headers) as C++ structs.
4. **Structured systems.** Rewrite systems (scheduler, player, collision,
   objects, camera, level loading) as classes, one at a time, each verified by
   lockstep before its generated version is retired.
5. **Native presentation.** Optional native rendering and audio (widescreen,
   higher resolution, a native music sequencer) once game logic is native.
6. **Other platforms.** The runtime is portable C++; new platforms need a
   video/audio/input layer and big-endian care on big-endian CPUs.

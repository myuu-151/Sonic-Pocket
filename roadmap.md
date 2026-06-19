# Project roadmap

The goal is a faithful native PC port of *Sonic the Hedgehog: Pocket
Adventure*, developed through reproducible binary analysis. An emulated PC
shell may be used as an intermediate milestone, but the final engine should
replace original game logic with maintainable native code.

## 1. Establish a reference environment

- [x] Record the reference cartridge image's size and hashes.
- [x] Verify the image against a recognized cartridge database.
- [x] Confirm that the game boots and runs in BizHawk 2.10.
- [ ] Pin exact MAME and Beetle NGP versions or commits.
- [ ] Run the reference image in MAME and Beetle NGP.
- [ ] Capture deterministic input recordings for representative scenes.
- [ ] Capture reference screenshots, frame timings, audio, and save data.
- [ ] Define a small regression corpus covering boot, menus, gameplay, and
      saving.

### Exit criteria

Given the reference ROM and recorded inputs, every supported reference emulator
can reproduce the expected checkpoints without manual interaction.

## 2. Map the original game

- [x] Document the Neo Geo Pocket Color memory map.
- [x] Verify the cartridge entry point at `0x200040`.
- [x] Produce the first TLCS-900/H entry-point disassembly.
- [x] Start a human-authored symbol database.
- [x] Identify the interrupt-handler structure at `0x2000A0`.
- [x] Install and pin Ghidra with the
      [TLCS-900/H processor module](https://github.com/nevesnunes/ghidra-tlcs900h)
      and
      [Neo Geo Pocket loader](https://github.com/nevesnunes/ghidra-neogeopocket-loader).
- [ ] Use MAME's debugger for breakpoints, instruction traces, register
      inspection, and memory watches.
- [x] Prove the interrupt source used by the handler at `0x2000A0`.
- [x] Identify startup, VBlank, input polling, and the main/frame loop.
- [ ] Label the title screen, object loop, collision, camera, level loading,
      animation, and sound-command systems.

Primary hardware references:

- [MAME TLCS-900 disassembler](https://github.com/mamedev/mame/blob/master/src/devices/cpu/tlcs900/dasm900.cpp)
- [MAME Neo Geo Pocket driver](https://github.com/mamedev/mame/blob/master/src/mame/snk/ngp.cpp)

### Exit criteria

The initialization and frame paths are mapped from the cartridge entry point to
the main game update, with confirmed symbols for input, interrupts, rendering,
and audio dispatch.

## 3. Build ROM-analysis tools

- [x] Build a cartridge-header and reference-hash verifier.
- [ ] Add a pointer and data-table scanner.
- [x] Build tile and palette rendering for the first representative stage.
- [x] Build full-plane and collision-map viewers for the first representative
      stage.
- [ ] Build sprite and animation viewers.
- [ ] Identify compression formats and implement decompressors.
- [ ] Build a MAME trace importer connected to the symbol database.
- [x] Build an asset extractor that requires and verifies the user's original
      ROM.
- [ ] Document every discovered data format with ROM addresses and validation
      examples.

Generated assets and full-ROM disassembly remain local and must not be committed
to the public repository.

### Exit criteria

The reference ROM can be transformed locally into documented intermediate data
for at least one complete stage, without embedding copyrighted assets in the
repository.

## 4. Create the PC shell

- [x] Establish a C++20 and CMake project.
- [ ] Integrate SDL3 windowing, input, gamepad, audio, and configuration.
- [x] Render a 160 by 152 off-screen target with integer scaling.
- [ ] Add deterministic fixed-step execution and input recording/playback.
- [ ] Optionally embed Beetle NGP as an intermediate compatibility backend.
- [ ] Add developer overlays for collision, objects, camera, and performance.
  - [x] Add the first collision overlay and camera-position viewer controls.
  - [ ] Add object and performance overlays.

[Beetle NGP](https://github.com/libretro/beetle-ngp-libretro) already implements
the TLCS-900/H, Z80, video, audio, memory, flash, and HLE BIOS behavior. It is
[GPLv2](https://github.com/libretro/beetle-ngp-libretro/blob/master/COPYING), so
directly incorporating its code requires a GPL-compatible project and release
plan.

### Exit criteria

The PC executable can run deterministic test scenes, display the native
resolution correctly, accept recorded and live input, and expose comparable
state for regression testing.

## 5. Implement the native engine

- [x] Implement the first native controllable Sonic prototype with ROM-extracted
      sprite art, prototype animation frames, gravity, jump input, camera
      follow, and generated stage collision mask.
- [x] Split prototype animation into explicit idle/run/jump/fall states with
      state-transition timing.
- [x] Port the first ROM-shaped player movement primitives into the native
      viewer: signed ground speed, ROM sine lookup, grounded acceleration,
      skid/reversal, jump setup timing, `PlrAirDrag`-style air control, and
      `sub_39ABEC`-style air-to-ground speed projection.
- [ ] Implement 60 Hz presentation with the observed deterministic 30 Hz
      player gameplay tick; verify cadence for other subsystems.
- [ ] Reimplement object scheduling and state.
- [ ] Reimplement player physics and collision.
  - [x] Runtime-match the early grounded movement trace through the first major
        jump/ramp approach.
  - [x] Preserve over-cap ramp momentum in air instead of clamping to the normal
        air-control cap.
  - [x] Convert landing X/Y velocity back into ROM-style ground speed using the
        mapped `sub_39ABEC` behavior.
  - [ ] Port `Plr_IsOnGround` / landing contact selection in full; the current
        known mismatch is native selecting ramp angle `0x13` where the ROM
        selects `0x0F` during a traced Neo South Island Act 1 ramp landing.
  - [ ] Port the remaining `sub_39B508` and `Plr_CheckNoGrnd` collision/support
        paths without per-ramp geometry fixes.
- [ ] Reimplement the camera, stage loading, and level events.
- [ ] Reimplement animation, rendering, menus, and save data.
- [ ] Complete one representative stage before expanding to the full game.
- [ ] Replace temporary emulated systems incrementally.

The first renderer should preserve original tile and sprite behavior through a
160 by 152 off-screen target. Presentation enhancements come only after
behavioral parity.

### Exit criteria

One complete stage is playable using native engine logic and passes the defined
behavioral checkpoints against the reference game.

## 6. Reconstruct audio

- [ ] Extract the game's music and sound-command tables.
- [ ] Use
      [ValleyBell's `ngp2mid`](https://github.com/ValleyBell/MidiConverters/blob/master/ngp2mid.c)
      research to document the Sonic Pocket Adventure sequence grammar.
- [ ] Implement notes, delays, instruments, detune, pan, volume, loops, tempo,
      and subroutines in a native sequencer.
- [ ] Initially retain emulated Z80/T6W28 playback for accuracy.
- [ ] Map instrument envelopes, modulation, DAC use, and sound-effect dispatch.
- [ ] Compare native register/event streams against the reference driver.

MIDI conversion is useful for analysis but does not preserve every PSG timbre,
envelope, or modulation detail. Chip-level comparison remains the fidelity
target.

### Exit criteria

Music and sound effects match reference timing and behavior closely enough to
pass automated event-stream checks and listening comparisons.

## 7. Validate continuously

- [ ] Replay identical input scripts in the reference game and native port.
- [ ] Compare position, velocity, animation, rings, object state, and camera
      state at checkpoints.
- [ ] Compare frame output at selected deterministic checkpoints.
- [ ] Compare audio events and save-data output.
- [x] Run ROM-free tooling tests in continuous integration.
- [ ] Add native-engine regression tests as each subsystem lands.

Validation is part of every phase rather than a final cleanup step. New native
behavior is not considered complete until it can be compared against a pinned
reference result.

## Current focus

The project is currently between phase 2 and phase 5: mapping continues, but
the PC viewer now contains real ROM-shaped player routines instead of only a
feel-based prototype. Startup, VBlank, input polling, task scheduling, core
player movement, spindash, roll, jump setup, air drag, landing speed projection,
and the first collision pass are mapped or partially ported. Full-height jump
timing is runtime-confirmed. Hurt, death, and vertical spring behavior are also
mapped from a directed runtime trace. The camera origin, stage bounds, and
static follow logic are mapped and cross-checked against ValleyBell's public
SPA disassembly release.

A ROM-driven extractor reconstructs Neo South Island Act 1's two tile planes,
collision values, palettes, and 94 object placements; all extracted binary
regions are validated byte-for-byte against that public reference. The native
SDL3 executable displays the reconstructed stage through an integer-scaled
160 by 152 viewport and has controllable Sonic using ROM-extracted sprites,
state-based animations, skid dust, slope attachment, jumping, air control, and
camera follow.

The immediate target is no longer guessing slope constants. The next required
port is the ROM's ground contact path, especially `Plr_IsOnGround` and the
remaining `sub_39B508` / `Plr_CheckNoGrnd` support logic. The current trace
shows the native viewer matching the ROM through the jump/ramp approach, then
diverging on a landing contact where native chooses ramp angle `0x13` and the
ROM chooses `0x0F`. That must be fixed by porting the shared contact-selection
routines, not by special-casing individual ramps.

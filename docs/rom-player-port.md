# ROM player movement port notes

Source reference:

- `SonicPocketAdventure_disasm+tools/disassembly/spa.asm`
- `SonicPocketAdventure_disasm+tools/other-material/RAMOffsets.txt`

## Player object fields

The disassembly uses `XIZ` as the current player object pointer. The useful
fields for the native viewer are:

- `XIZ+10h` / RAM `6718`: player rotation / surface angle
- `XIZ+12h`: X position, 16.8 fixed point integer word at `671A-671B`
- `XIZ+16h`: Y position, 16.8 fixed point integer word at `671E-671F`
- `XIZ+18h` / RAM `6720-6721`: ground speed
- `XIZ+1Ah` / RAM `6722-6723`: X speed, 8.8 fixed point
- `XIZ+1Ch` / RAM `6724-6725`: Y speed, 8.8 fixed point
- `XIZ+14h`, bit 7: facing left
- `XIZ+0Bh`, bit 2: rolling flag
- `XIZ+29h`: collision plane

## Walking call order

`Plr_Walking` at `spa.asm` around `00399EF2` runs the main grounded player
logic in this order:

1. `sub_399C88`
   - Applies slope force to `XIZ+18h` ground speed.
   - Calls `sub_399CB1`.
   - Clamps ground speed to `±0F00h`.
2. `sub_399443`
   - Applies player left/right input, acceleration, friction, speed caps, and
     skid setup to `XIZ+18h`.
3. `Plr_CheckRoll`
4. `Plr_CalcXYSpeed`
   - Converts angle + ground speed to X/Y speed using `DoSineLookup`.
   - Writes `XIZ+1Ah` and `XIZ+1Ch`.
   - Calls `Obj_AddSpeedXY` unless object flags bit 7 is set.
5. `sub_39B508`
   - Ground movement / collision application.
6. `sub_3993CE`
   - Platform/carry adjustment.
7. `Plr_CheckStop`
8. `Plr_CheckNoGrnd`
9. `Plr_CheckPush`
10. `Plr_CheckJump`
11. `SetupObjAni`

This order matters. A direct swap from the viewer's float trig to ROM
`DoSineLookup` caused trace drift because the current viewer still updates
ground speed and velocity in a different order in a few slope transitions.

## Extracted primitive: DoSineLookup

`DoSineLookup` is at `spa.asm` around `002914A0`; its source table `SineVals`
starts at ROM file offset `0x91214`.

Inputs:

- `A`: angle `00h..FFh`
- `IX`: fixed-point factor, where `0100h` is `1.0`

Outputs:

- `WA = cos(A) * IX / 0100h`
- `BC = sin(A) * IX / 0100h`

The C++ helper `rom_do_sine_lookup()` in `src/viewer/main.cpp` is a direct
translation of this primitive. It is intentionally not yet the active movement
path until `sub_399C88`, `sub_399443`, and `Plr_CalcXYSpeed` are ported as one
ordered block.

## Next port targets

Port these as a grouped replacement for the current guessed grounded physics:

1. `sub_399CB1`
2. `sub_399C88`
3. `sub_399443`
4. `Plr_CalcXYSpeed`
5. `sub_39ABEC` for air-to-ground speed conversion

Keep the replay harness as the regression check after each replacement.

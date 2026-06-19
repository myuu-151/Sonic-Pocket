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
`DoSineLookup` caused trace drift until the viewer also switched to the ROM's
signed ground-speed representation and grouped slope/input/velocity updates in
this order.

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
translation of this primitive. It is now used by the active grounded movement
path through `rom_plr_calc_xy_speed()`.

## Active grounded movement port

The viewer now keeps `Player::ground_speed` as the ROM's raw signed `XIZ+18h`
word and keeps `Player::facing_left` as the independent `XIZ+14h` bit 7 flag.
The active grounded update follows the ROM order:

1. `sub_399CB1`
2. `sub_399C88`
3. `sub_399443`
4. `Plr_CalcXYSpeed`

The trace harness replayed `out/player-runtime-trace.csv` with no mismatch
across 845 rows after this refactor. This specifically covers:

- the first uphill/right slope where slope force pushes speed above `0800h`
  without additional input acceleration stacking on top;
- the loop/wall reattach where the ROM has negative ground speed while the
  facing-left bit is clear;
- the later left-input section where the ROM flips the facing bit while keeping
  positive ground speed.

## Next port targets

Port the remaining movement/collision routines in ROM order:

1. `sub_39B508` ground movement / collision application
2. `Plr_CheckNoGrnd` support checks
3. `Plr_CheckJump`
4. `sub_39ABEC` for air-to-ground speed conversion

Keep the replay harness as the regression check after each replacement.

## Representation mismatch fixed during signed-speed refactor

The old viewer mostly treated `Player::ground_speed` as a positive magnitude
and used `Player::facing_left` to derive signed movement. The ROM does not work
that way consistently:

- `XIZ+18h` is a raw signed ground-speed word.
- `XIZ+14h` bit 7 is still an independent facing flag.
- `Plr_CalcXYSpeed` adds `80h` to the angle if facing left, then passes the raw
  signed `XIZ+18h` to `DoSineLookup`.
- `sub_399C88/sub_399CB1` also adjusts the angle by facing, but subtracts the
  resulting slope delta from the raw signed `XIZ+18h`.

This matters on the NSI loop/ramp trace:

- After the wall/loop reattach, the ROM has negative ground speed while the
  facing-left bit is not set.
- Later, while holding left on flat ground, the ROM has positive ground speed
  while the facing-left bit is set.

The viewer state was refactored accordingly:

1. Store raw signed ground speed.
2. Keep facing as a separate flag.
3. Replace magnitude checks with `abs(ground_speed)`.
4. Port `sub_399C88`, `sub_399443`, and `Plr_CalcXYSpeed` together.

Activating only `sub_399C88` against the old magnitude-based state regressed the
trace. The fix was to switch the state representation first, then replace the
slope/input/velocity functions as one ordered block.

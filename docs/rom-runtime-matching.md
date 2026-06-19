# ROM runtime matching plan

The viewer should no longer be tuned by feel. The ROM trace is the source of
truth, and native code changes should be accepted only when they match captured
runtime fields frame-by-frame.

## Current baseline

- The viewer renders the extracted Neo South Island Act 1 assets and has a
  controllable Sonic.
- Animation bands for walk, run, and max-speed peelout are disassembly-backed.
- Movement is no longer only a simplified stand-in. The active viewer has
  ROM-shaped signed ground speed, facing flags, grounded acceleration,
  skid/reversal, jump setup timing, `PlrAirDrag`-style air control, and
  `sub_39ABEC`-style air-to-ground speed projection.
- Slope/contact parity is still incomplete. The current known mismatch is a
  traced Neo South Island Act 1 ramp landing where the native viewer selects
  surface angle `0x13` while the ROM selects `0x0F`. This points at
  `Plr_IsOnGround` / contact probe selection, not a single bad ramp tile.

## Existing runtime evidence

`out/player-runtime-trace.csv` already contains useful movement evidence:

- Gameplay/player state advances on a 30 Hz cadence over the 60 Hz video output.
- Slope contact is present. The trace includes angle changes such as
  `0x00 -> 0x0A`, `0x00 -> 0xF6`, and later airborne angle relaxation.
- Reversal/skid evidence is present around frame `3200`.
- Camera columns are not present in this older CSV, so camera follow needs a
  fresh capture with the current Lua tracer.

Key reversal window from the existing trace:

| Frame | Buttons | Movement flags | Ground speed | X velocity | Meaning |
| ---: | --- | ---: | ---: | ---: | --- |
| 3198 | none | `0x10` | 1940 | 1940 | coasting right |
| 3200 | Left | `0x10` | 1652 | 1652 | opposite input applies `0x120` decel |
| 3202 | Left | `0x10` | 1364 | 1364 | still sliding right |
| 3204 | Left | `0x10` | 1076 | 1076 | still sliding right |
| 3206 | Left | `0x10` | 788 | 788 | still sliding right |
| 3208 | Left | `0x10` | 500 | 500 | below strong-skid threshold |
| 3210 | Left | `0x10` | 212 | 212 | almost stopped |
| 3212 | Left | `0x90` | 76 | -76 | facing/movement flag flips; velocity goes left |
| 3214 | Left | `0x90` | 108 | -108 | accelerating left |

This proves the native viewer cannot represent player motion as only a signed
`ground_speed`. The ROM uses a scalar speed plus movement/facing flags, then
derives X/Y velocity from that state and the surface angle.

## Required implementation order

1. Replace the viewer's temporary player model with a ROM-shaped player state:
   task flags, movement flags, surface angle, 16.8 position, ground speed, X/Y
   velocity, collision radii, and animation counters.
   - Status: partially complete for the active viewer state needed by the
     current movement trace.
2. Port the flat-ground walking/reversal path from the ROM routines:
   `player_state_walk` at `0x399EF2`, `sub_399443`, and its skid helper.
   - Status: implemented in the viewer and verified against the existing trace
     windows used during movement work.
3. Verify against the existing trace window around frame `3200` before changing
   slopes or camera.
4. Port angle-to-velocity conversion and slope attachment/collision from the
   traced slope windows.
   - Status: angle-to-velocity and several slope attachment pieces are ported,
     but contact selection still diverges on ramp landing.
5. Port the ROM ground-contact routines instead of adding ramp-specific fixes:
   `Plr_IsOnGround`, the remaining `sub_39B508` collision paths, and
   `Plr_CheckNoGrnd`.
6. Capture a fresh camera trace and port horizontal camera follow:
   `update_player_camera_follow` at `0x39C9D9`.
7. Only after movement/camera fields match should the viewer animation states
   be judged visually.

## Current trace status

Recent native replay work moved the first physical mismatch much later in the
recording:

- Air control now preserves ROM over-cap ramp momentum through the jump arc.
  The previous native behavior clamped X speed toward `0x800` too early.
- Landing speed now uses a ROM-shaped `sub_39ABEC` projection, so the ground
  speed at the ramp landing matches the ROM value of `992`.
- The remaining mismatch at frame `1636` is contact selection and transition
  velocity:
  - ROM: surface angle `0x0F`, Y position `111926`, Y velocity `-1792`
  - native: surface angle `0x13`, Y position `112182`, Y velocity transition
    still differs after the contact pass

That mismatch should be fixed by porting the shared ROM contact routines, not
by changing individual level geometry or adding per-ramp exceptions.

## Trace analyzer

Use:

```powershell
python tools\analyze_player_trace.py out\player-runtime-trace.csv --max-events 80
python tools\analyze_player_trace.py out\player-runtime-trace.csv --window 3200 --radius 24
```

The analyzer does not emulate the game. It only identifies runtime evidence and
prints the ROM values that the native port must match.

## Fresh captures still needed

Use `scripts/bizhawk-player-trace.lua` from a save state inside Neo South
Island Act 1.

1. Camera trace: stand still, run right until the camera moves, reverse left,
   and keep holding left until the camera follow target has clearly moved.
2. Clean flat reversal trace: on flat ground, hold Right until max ground
   speed, immediately hold Left until Sonic fully reverses, then release.
3. Slope trace: hold Right through the first slope/loop section without
   jumping, then release on flat ground.

Commit or save each CSV under `out/` with a descriptive name so the comparator
can target one behavior at a time.

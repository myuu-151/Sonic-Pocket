# Player runtime trace 001

This note summarizes the first capture made with
`scripts/bizhawk-player-trace.lua` under BizHawk 2.10. The generated CSV stays
in the ignored `out/` directory; only human-authored observations are tracked.

## Capture bounds

- 1,027 rows covering emulator frames 623 through 1649.
- Initial player state: idle at X 112, Y 437.
- Actions: run right over a slope, reverse briefly, crouch, charge a spindash
  with three A presses, release into a roll, traverse curved terrain, become
  airborne, and land.
- No ordinary standing jump was captured; that remains the next directed test.

## Gameplay cadence

All 513 even-to-odd frame pairs are identical across state, input, position,
velocity, angle, flags, and radii. For example, values on frames 904 and 905
match, then advance on frame 906 and repeat on 907. The observed player logic
therefore advances every two video frames in this scene.

The native port should initially model 60 Hz presentation with a deterministic
30 Hz gameplay tick. Other tasks and stages still need tracing before assuming
that every subsystem shares the same cadence.

## Confirmed checkpoints

| Frame | State | Flags | Angle | Position | Ground / X / Y velocity | Radii | Observation |
| ---: | --- | ---: | ---: | --- | --- | --- | --- |
| 904 | `0x399EF2` | `0x01` | `0x0A` | 178, 438 | 1024 / 1024 / 0 | 7 by 13 | First sampled frame on the rising slope. |
| 906 | `0x399EF2` | `0x01` | `0x0A` | 181, 439 | 1045 / 1012 / 253 | 7 by 13 | Angle-resolved velocity advances both axes. |
| 1048 | `0x399B19` | `0x05` | `0x00` | 356, 450 | 0 / 0 / 0 | 7 by 10 | Crouch state and compact bounds active. |
| 1158 | `0x399BB7` | `0x05` | `0x00` | 356, 450 | 0 / 0 / 0 | 7 by 10 | Spindash charge state active. |
| 1202 | `0x399F3B` | `0x05` | `0x00` | 356, 450 | 2944 / 0 / 0 | 7 by 10 | Down release selects roll speed `0x0B80`. |
| 1204 | `0x399F48` | `0x05` | `0x00` | 367, 450 | 2928 / 2928 / 0 | 7 by 10 | First sampled rolling movement frame. |
| 1268 | `0x39AAF7` | `0x07` | `0x3C` | 688, 478 | 2508 / 0 / 2380 | 7 by 10 | Airborne bit added after leaving curved terrain. |
| 1344 | `0x399EE5` | `0x07` | `0x00` | 705, 466 | 183 / 183 / -2484 | 7 by 10 | Landing selects the grounded walk entry. |
| 1346 | `0x3999F7` | `0x01` | `0x00` | 706, 469 | 151 / 151 / 0 | 7 by 13 | Grounded bounds and flags restored. |

## State sequence

The capture directly validates this normal-action chain:

```text
idle -> walk -> crouch -> spindash charge -> roll
     -> enter airborne -> airborne -> walk entry -> idle
```

It also validates that `player_enter_airborne` at `0x39AAC6` is the common
transition used when grounded collision loses support; it adds task flag bit 1
before `player_state_airborne` at `0x39AAF7` executes.

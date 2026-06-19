# Player runtime trace 003

This BizHawk 2.10 capture records damage knockback, a zero-ring death, and a
vertical spring launch. The generated CSV remains ignored under `out/`.

## Capture bounds

- 2,576 rows covering emulator frames 2073 through 4648.
- Player values again repeat for two video frames, confirming the observed
  30 Hz player update cadence.
- Damage is captured twice, at frames 2272 and 4078.
- Death begins at frame 2572 and the player task is removed at frame 2734.
- The spring changes the player state and vertical velocity at frame 4314.

## Damage knockback

The first damage event occurs while Sonic is moving right on level ground:

| Frame | State | Flags | X / Y | X velocity | Y velocity | Observation |
| ---: | --- | ---: | --- | ---: | ---: | --- |
| 2270 | `player_state_walk` | `0x01` | 759 / 469 | 736 | 0 | Last undamaged grounded update. |
| 2272 | `player_enter_hurt` | `0x01` | 762 / 469 | 704 | 0 | Damage selects the hurt entry state. |
| 2274 | `player_state_airborne` | `0x03` | 760 / 474 | -512 | 1408 (`0x0580`) | Knockback is active after one gravity step. |
| 2296 | `player_state_airborne` | `0x03` | 739 / 502 | -407 | 0 | Knockback apex. |
| 2320 | `player_enter_walk` | `0x03` | 724 / 469 | 0 | 0 | Floor contact ends the damage arc. |
| 2322 | `player_state_walk` | `0x01` | 724 / 469 | 32 | 0 | Grounded flags are restored. |

Static code at `player_enter_hurt` confirms a horizontal knockback magnitude
of `0x0200`, directed away from the damaging object, and a normal vertical
impulse of `0x0600`. The common airborne update immediately subtracts
`0x0080`, producing the first logged `0x0580`. Task offset `+0x48` is set to
`0x3C`, consistent with a 60-gameplay-tick post-hit invulnerability timer.

## Death sequence

| Frame | State | Flags | Y | Y velocity | Observation |
| ---: | --- | ---: | ---: | ---: | --- |
| 2572 | `player_enter_death` | `0x01` | 469 | 0 | A later zero-ring hit selects death. |
| 2574 | `player_state_death_motion` | `0x03` | 476 | 1920 (`0x0780`) | `0x0800` launch after one gravity step. |
| 2604 | `player_state_death_motion` | `0x03` | 529 | 0 | Death-arc apex. |
| 2656 | `player_state_death_delay` | `0x03` | 353 | -3328 (`-0x0D00`) | Motion freezes after crossing the camera boundary. |
| 2734 | task removed | `0x03` | 353 | -3328 | Player task function reads `0xFFFFFFFF`. |
| 2836 | player initialization | `0x01` | 437 | 0 | The stage restart creates the player again. |

`player_enter_death` clears horizontal velocity, sets movement flags to
`0x18`, applies a normal `0x0800` vertical impulse, and enters
`player_state_death_motion`. That state subtracts `0x0080` gravity and
integrates only Y until the camera-boundary check selects
`player_state_death_delay`.

## Vertical spring

| Frame | State | Flags | Y | Y velocity | Observation |
| ---: | --- | ---: | ---: | ---: | --- |
| 4312 | `player_state_airborne` | `0x07` | 252 | -1664 | Player is descending toward the spring. |
| 4314 | `player_enter_spring_launch` | `0x07` | 245 | 3328 (`0x0D00`) | The spring reverses motion and supplies its launch velocity. |
| 4316 | `player_state_airborne` | `0x03` | 260 | 3200 (`0x0C80`) | Spring entry selects its animation, clears compact mode, and resumes airborne physics. |
| 4350 | `player_state_airborne` | `0x03` | 394 | 0 | Spring-launch apex. |
| 4378 | `player_enter_walk` | `0x03` | 349 | -1792 | Floor contact ends the spring arc. |

The vertical spring impulse observed here is `0x0D00`. The spring object
supplies that velocity before selecting `player_enter_spring_launch`; the
player entry routine selects the spring animation, applies airborne flags, and
transfers immediately to the common airborne state.

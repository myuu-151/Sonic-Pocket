# Player runtime trace 002

This directed BizHawk 2.10 capture records one standing jump on flat ground.
The generated CSV stays in the ignored `out/` directory.

## Capture bounds

- 425 rows covering emulator frames 1649 through 2073.
- Sonic begins and ends idle at X 706, Y 469.
- A is pressed on frame 1774 and released on frame 1842, just before landing.
- No horizontal input occurs during the jump.

As in trace 001, each gameplay value repeats for two video frames. The values
below list the even frame where each 30 Hz gameplay update first appears.

## Jump checkpoints

| Frame | State | A held | Flags | Y raw / integer | Y velocity | Radius Y | Observation |
| ---: | --- | --- | ---: | --- | ---: | ---: | --- |
| 1774 | `0x39AA86` | yes | `0x01` | 120124 / 469 | 0 | 13 | Jump entry selected by the new A press. |
| 1776 | `0x39AAEB` | yes | `0x07` | 121532 / 474 | 2176 (`0x0880`) | 10 | Airborne compact bounds; `0x0900` impulse minus one gravity step. |
| 1808 | `0x39AAEB` | yes | `0x07` | 138940 / 542 | 128 (`0x0080`) | 10 | Last positive-velocity update before the apex. |
| 1810 | `0x39AAEB` | yes | `0x07` | 138940 / 542 | 0 | 10 | Full-height apex. |
| 1812 | `0x39AAEB` | yes | `0x07` | 138812 / 542 | -128 (`-0x0080`) | 10 | First descending update. |
| 1842 | `0x39AAF7` | no | `0x07` | 121532 / 474 | -2048 (`-0x0800`) | 10 | A release leaves the held-jump state. |
| 1844 | `0x39AAF7` | no | `0x07` | 119356 / 466 | -2176 (`-0x0880`) | 10 | Last free-fall position before floor resolution. |
| 1846 | `0x399EE5` | no | `0x07` | 119356 / 466 | -2304 (`-0x0900`) | 10 | Floor collision selects grounded walk entry. |
| 1848 | `0x3999F7` | no | `0x01` | 120124 / 469 | 0 | 13 | Standing bounds and idle state restored. |

## Confirmed model

For a normal full-height jump from level ground:

```text
jump impulse:       +0x0900
gravity per tick:   -0x0080
gameplay tick:      30 Hz observed
standing bounds:    7 by 13
airborne bounds:    7 by 10
airborne task bit:  0x02
compact task bit:   0x04
```

Y increases upward in the game's coordinate system. The collision-shape
transition moves the player center by three pixels while preserving the foot
contact point.

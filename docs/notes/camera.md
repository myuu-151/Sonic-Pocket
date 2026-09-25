# Camera system

The static camera pass identifies the Plane 2 world-space origin, stage
bounds, and player screen-position targets. These names and call relationships
were cross-checked against ValleyBell's public SPA disassembly release.

## Camera state

| RAM address | Working name | Meaning |
| ---: | --- | --- |
| `0x506C` | `plane2_camera_x` | Plane 2 horizontal world-space origin. |
| `0x506E` | `plane2_camera_y` | Plane 2 vertical world-space origin. |
| `0x507A` | `plane2_camera_x_start` | Horizontal start limit at camera structure offset `+0x0E`. |
| `0x507C` | `plane2_camera_x_end` | Horizontal end limit at camera structure offset `+0x10`. |
| `0x507E` | `plane2_camera_y_end` | Vertical end limit at camera structure offset `+0x12`. |
| `0x5080` | `plane2_camera_y_start` | Vertical start limit at camera structure offset `+0x14`. |
| `0x67A4` | `player_screen_x_current` | Current player horizontal position within the viewport. |
| `0x67A6` | `player_screen_y_current` | Current player vertical position within the viewport. |
| `0x67B8` | `player_screen_x_target` | Optional horizontal target override. |
| `0x67BA` | `player_screen_y_target` | Desired vertical viewport position. |

`initialize_camera_horizontal_follow` at `0x39D0D5` directly establishes:

```text
player_screen_x_current = player_x - plane2_camera_x
```

The render and map-upload paths both read `camera_x` and `camera_y`, while
player/object transforms subtract those values from world coordinates.

## Horizontal following

`update_player_camera_follow` at `0x39C9D9` first uses
`player_screen_x_target` when it is nonzero. Otherwise it selects 48 pixels
when player sprite/facing bit 7 is clear and 112 pixels when bit 7 is set. The
current screen position moves toward that target by no more than two pixels
per player tick.

The routine derives a camera correction from the player world X and the
current follow offset, then clamps the resulting origin to
`camera_min_x..camera_max_x`. Another flag prevents backward camera movement
under normal stage conditions.

This is a look-ahead camera rather than a permanently centered camera: Sonic
can occupy either side of the 160-pixel viewport, and the framing target
slides gradually rather than snapping.

## Vertical following

`apply_vertical_camera_follow` at `0x39C960` compares player Y with
`camera_follow_y_offset`, applies the compact-shape three-pixel adjustment,
and clamps the resulting camera correction to
`camera_min_y..camera_max_y`.

The vertical screen position itself is constrained to 8 through 136 pixels.
`update_player_camera_follow` moves it toward `camera_follow_y_target` by no
more than four pixels per player tick. Static code also includes conditions
that suppress downward or backward correction; these likely cover grounded
camera stability, death, scripted movement, and stage-specific locks.

## Runtime validation

The BizHawk player tracer records all camera fields. A directed capture remains
useful as a regression fixture and to identify stage-specific locks:

1. Stand still, then run right at full speed for several seconds.
2. Reverse and run left for several seconds.
3. Jump at the bottom of a tall arc or use a vertical spring.
4. Continue until the camera reaches a stage boundary.

The capture will show the exact runtime timing of screen-target changes,
vertical tracking, stage locks, and whether camera values update on the same
30 Hz player cadence.
